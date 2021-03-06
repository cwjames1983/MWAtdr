#include "InternodeCommunication.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <map>
#include <memory>
#include <numeric>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <mpi.h>

#include "ChannelRemapping.hpp"
#include "Common.hpp"
#include "NodeAntennaInputAssigner.hpp"


// Raises an exception if the MPI error code does not indicate success.
// Note that we don't expect this to ever happen, unless there is a logic error in our code.
#define assertMPISuccess(mpiErrorCode) { \
    auto const evaldCode = (mpiErrorCode); \
    if (evaldCode != MPI_SUCCESS) { \
        throw InternodeCommunicationError{ \
            "MPI call failed with code " + std::to_string(evaldCode) + " (" __FILE__ " : " + std::to_string(__LINE__) + ")"}; \
    }}


std::variant<PrimaryNodeCommunicator, SecondaryNodeCommunicator> InternodeCommunicationContext::getCommunicator() {
    int nodeID = 0;
    assertMPISuccess(MPI_Comm_rank(MPI_COMM_WORLD, &nodeID));
    if (nodeID == 0) {
        return PrimaryNodeCommunicator{shared_from_this()};
    }
    else {
        return SecondaryNodeCommunicator{shared_from_this()};
    }
}

std::shared_ptr<InternodeCommunicationContext> InternodeCommunicationContext::initialise() {
    return {new InternodeCommunicationContext{}, std::default_delete<InternodeCommunicationContext>{}};
}


InternodeCommunicationContext::MPIContext::MPIContext() {
    // MPI may only be initialised once.
    if (_initialised.test_and_set()) {
        throw std::logic_error{"Internode communication may only be initialised once."};
    }
    else {
        int providedThreadSupport = 0;
        // Need concurrent MPI usage for error status communication.
        assertMPISuccess(MPI_Init_thread(nullptr, nullptr, MPI_THREAD_MULTIPLE, &providedThreadSupport));
        if (providedThreadSupport < MPI_THREAD_MULTIPLE) {
            throw InternodeCommunicationError{"MPI failed to provide required thread support."};
        }
    }
}

InternodeCommunicationContext::MPIContext::~MPIContext() {
    MPI_Finalize();
}

std::atomic_flag InternodeCommunicationContext::MPIContext::_initialised = ATOMIC_FLAG_INIT;


InternodeCommunicationContext::ErrorCommunicator::ErrorCommunicator() :
    _communicator{_createCommunicator()}, _errorStatus{false}, _thread{&ErrorCommunicator::_threadFunc, this}
{}

InternodeCommunicationContext::ErrorCommunicator::~ErrorCommunicator() {
    // Disable the warning about throwing exceptions in destructors causing program termination.
    // That's exactly the behaviour we want (we cannot recover from an MPI error, which should never occur anyway).
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wterminate"

    // Barrier just to make sure no nodes are in the process of sending an error status message.
    assertMPISuccess(MPI_Barrier(_communicator));

    int nodeID = 0;
    assertMPISuccess(MPI_Comm_rank(_communicator, &nodeID));

    // Tell our background thread to exit.
    auto const message = static_cast<unsigned>(Message::EXIT_THREAD);
    assertMPISuccess(MPI_Send(&message, 1, MPI_UNSIGNED, nodeID, 0, _communicator));

#pragma GCC diagnostic pop

    _thread.join();

    MPI_Comm_free(&_communicator);
}

bool InternodeCommunicationContext::ErrorCommunicator::getErrorStatus() const {
    return _errorStatus;
}

void InternodeCommunicationContext::ErrorCommunicator::indicateError() {
    // Only need to do anything if no error has occurred yet.
    bool errorStatus = false;
    if (_errorStatus.compare_exchange_strong(errorStatus, true)) {
        // Tell all the other nodes that an error has occurred.
        int nodeCount = 0;
        assertMPISuccess(MPI_Comm_size(_communicator, &nodeCount));
        int thisNode = 0;
        assertMPISuccess(MPI_Comm_rank(_communicator, &thisNode));
        for (int node = 0; node < nodeCount; ++node) {
            if (node != thisNode) {
                auto const message = static_cast<unsigned>(Message::ERROR_OCCURRED);
                assertMPISuccess(MPI_Send(&message, 1, MPI_UNSIGNED, node, 0, _communicator));
            }
        }
    }
}

void InternodeCommunicationContext::ErrorCommunicator::_threadFunc() {
    while (true) {
        Message message{};
        assertMPISuccess(MPI_Recv(&message, 1, MPI_UNSIGNED, MPI_ANY_SOURCE, 0, _communicator, MPI_STATUS_IGNORE));
        if (message == Message::EXIT_THREAD) {
            // The main thread has told us to exit.
            break;
        }
        else if (message == Message::ERROR_OCCURRED) {
            // Some node has told us an error has occurred.
            _errorStatus = true;
        }
    }
}

MPI_Comm InternodeCommunicationContext::ErrorCommunicator::_createCommunicator() {
    MPI_Comm communicator;
    assertMPISuccess(MPI_Comm_dup(MPI_COMM_WORLD, &communicator));
    return communicator;
}


InternodeCommunicationContext::~InternodeCommunicationContext() {
    // Disable the warning about throwing exceptions in destructors causing program termination.
    // That's exactly the behaviour we want (we cannot recover from an MPI error, which should never occur anyway).
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wterminate"

    // Do a barrier just to make sure everything else is probably done.
    assertMPISuccess(MPI_Barrier(MPI_COMM_WORLD));

#pragma GCC diagnostic pop
}


InternodeCommunicator::InternodeCommunicator(std::shared_ptr<InternodeCommunicationContext> context) :
    _context{context}
{}

std::shared_ptr<InternodeCommunicationContext> const& InternodeCommunicator::getContext() const {
    return _context;
}

unsigned InternodeCommunicator::getNodeID() const {
    int id = 0;
    assertMPISuccess(MPI_Comm_rank(MPI_COMM_WORLD, &id));
    return id;
}

unsigned InternodeCommunicator::getNodeCount() const {
    int count = 0;
    assertMPISuccess(MPI_Comm_size(MPI_COMM_WORLD, &count));
    return count;
}

void InternodeCommunicator::synchronise() const {
    assertMPISuccess(MPI_Barrier(MPI_COMM_WORLD));
}

bool InternodeCommunicator::getErrorStatus() const {
    return _context->_errorCommunicator.getErrorStatus();
}

void InternodeCommunicator::indicateError() {
    _context->_errorCommunicator.indicateError();
}


// For the following communication functions, I will note that I am not an MPI expert.
// There are likely better ways to do some of this communication, for example using custom MPI datatypes for our
// structs. However, my approach has been to stick to the simplest MPI functionality for speed and ease of development.
// Apologies in advance.


PrimaryNodeCommunicator::PrimaryNodeCommunicator(std::shared_ptr<InternodeCommunicationContext> context) :
    InternodeCommunicator{context}
{
    if (getNodeID() != 0) {
        throw std::logic_error{"PrimaryNodeCommunicator must only be used with node 0."};
    }
}

void PrimaryNodeCommunicator::sendAppStartupStatus(bool status) const {
    assertMPISuccess(MPI_Bcast(&status, 1, MPI_CXX_BOOL, 0, MPI_COMM_WORLD));
}

std::map<unsigned, bool> PrimaryNodeCommunicator::receiveNodeSetupStatus() const {
    auto const nodeCount = getNodeCount();
    std::vector<char> statuses(nodeCount);  // Can't use bool due to std::vector<bool> specialisation.
    // Note that the root node must send data, but actually we don't need anything from root, so we will discard it.
    char const dummyRootValue = true;
    assertMPISuccess(
        MPI_Gather(&dummyRootValue, 1, MPI_CHAR, statuses.data(), 1, MPI_CHAR, 0, MPI_COMM_WORLD));
    
    std::map<unsigned, bool> result;
    for (unsigned node = 1; node < nodeCount; ++node) {
        result.emplace(node, static_cast<bool>(statuses.at(node)));
    }
    return result;
}

void PrimaryNodeCommunicator::sendAppConfig(AppConfig const& appConfig) const {
    auto const& inputDirectoryPath = appConfig.inputDirectoryPath;
    auto const& invPolyphaseFilterPath = appConfig.invPolyphaseFilterPath;
    auto const& outputDirectoryPath = appConfig.outputDirectoryPath;

    // First we will send the fixed-size data, including sizes of the variable-size data (strings).
    std::array<unsigned long long, 6> part1Buffer{
        appConfig.observationID,
        appConfig.signalStartTime,
        appConfig.ignoreErrors,
        inputDirectoryPath.size(),
        invPolyphaseFilterPath.size(),
        outputDirectoryPath.size()
    };
    assertMPISuccess(MPI_Bcast(part1Buffer.data(), part1Buffer.size(), MPI_UNSIGNED_LONG_LONG, 0, MPI_COMM_WORLD));

    // Next we will put all the variable-size strings together and send them.
    std::vector<char> part2Buffer(inputDirectoryPath.size() + invPolyphaseFilterPath.size()
        + outputDirectoryPath.size());
    auto it = std::copy(inputDirectoryPath.cbegin(), inputDirectoryPath.cend(), part2Buffer.begin());
    it = std::copy(invPolyphaseFilterPath.cbegin(), invPolyphaseFilterPath.cend(), it);
    std::copy(outputDirectoryPath.cbegin(), outputDirectoryPath.cend(), it);
    assertMPISuccess(MPI_Bcast(part2Buffer.data(), part2Buffer.size(), MPI_CHAR, 0, MPI_COMM_WORLD));
}

void PrimaryNodeCommunicator::sendAntennaConfig(AntennaConfig const& antennaConfig) const {
    // First we send the sizes of the antenna input and frequency channel arrays.
    std::array<unsigned long long, 2> part1Buffer{
        antennaConfig.antennaInputs.size(),
        antennaConfig.frequencyChannels.size()
    };
    assertMPISuccess(MPI_Bcast(part1Buffer.data(), part1Buffer.size(), MPI_UNSIGNED_LONG_LONG, 0, MPI_COMM_WORLD));

    // Next we send the antenna inputs and frequency channels.
    // We can represent it all as an array of unsigned.
    std::vector<unsigned> part2Buffer;
    part2Buffer.reserve((3 * antennaConfig.antennaInputs.size() + antennaConfig.frequencyChannels.size()));
    for (auto const& antennaInputID : antennaConfig.antennaInputs) {
        part2Buffer.push_back(antennaInputID.tile);
        part2Buffer.push_back(static_cast<unsigned>(antennaInputID.signalChain));
        part2Buffer.push_back(static_cast<unsigned>(antennaInputID.flagged));
    }
    for (auto const channel : antennaConfig.frequencyChannels) {
        part2Buffer.push_back(channel);
    }
    assertMPISuccess(MPI_Bcast(part2Buffer.data(), part2Buffer.size(), MPI_UNSIGNED, 0, MPI_COMM_WORLD));
}

void PrimaryNodeCommunicator::sendChannelRemapping(ChannelRemapping const& channelRemapping) const {
    // First we will send the fixed-size data, include the size of the channel map.
    std::array<unsigned, 2> part1Buffer{
        channelRemapping.newSamplingFreq,
        static_cast<unsigned>(channelRemapping.channelMap.size())
    };
    assertMPISuccess(MPI_Bcast(part1Buffer.data(), part1Buffer.size(), MPI_UNSIGNED, 0, MPI_COMM_WORLD));

    // Next we will send the variable-size channel map.
    // We can represent single remapped channel as 3 unsigned ints.
    std::vector<unsigned> part2Buffer;
    part2Buffer.reserve(channelRemapping.channelMap.size() * 3);
    for (auto const& [oldChannel, remappedChannel] : channelRemapping.channelMap) {
        part2Buffer.push_back(oldChannel);
        part2Buffer.push_back(remappedChannel.newChannel);
        part2Buffer.push_back(remappedChannel.flipped);
    }
    assertMPISuccess(MPI_Bcast(part2Buffer.data(), part2Buffer.size(), MPI_UNSIGNED, 0, MPI_COMM_WORLD));
}

void PrimaryNodeCommunicator::sendAntennaInputAssignment(unsigned node,
        std::optional<AntennaInputRange> const& antennaInputAssignment) const {
    if (node == 0) {
        throw std::invalid_argument{"node must not be the primary node."};
    }
    
    // Represent the assignment as one array so we can send it all with one MPI call.
    std::array<unsigned, 3> buffer;
    if (antennaInputAssignment) {
        buffer = {true, antennaInputAssignment.value().begin, antennaInputAssignment.value().end};
    }
    else {
        buffer = {false, 0, 0};
    }
    assertMPISuccess(MPI_Send(buffer.data(), buffer.size(), MPI_UNSIGNED, node, 0, MPI_COMM_WORLD));
}

std::map<unsigned, ObservationProcessingResults> PrimaryNodeCommunicator::receiveProcessingResults() const {
    auto const nodeCount = getNodeCount();

    // We use a series of gathers to get each part of the results from the secondary nodes at once, rather than getting
    // the data from one secondary node at a time.
    // The result data includes multiple levels of nested variable-size data, which makes this code rather complex.
    // Before getting any actual data from the nodes, we have to first get the sizes of the variable-size data. These
    // sizes can vary on a per-node basis, thus we have to use MPI_Gatherv, and calculate displacements for storing the
    // collated data here.
    // Unfortunately this code is on the border of being too complex to reason about. However, it does work, as shown by
    // the unit tests.

    // First we will get the number of antenna inputs from each secondary node.
    std::vector<int> antennaInputCounts(nodeCount);
    // Note that the root node must send data, but actually we don't need anything from root.
    int const dummyRootAntennaInputCount = 0;
    assertMPISuccess(MPI_Gather(&dummyRootAntennaInputCount, 1, MPI_INT, antennaInputCounts.data(), 1, MPI_INT, 0,
        MPI_COMM_WORLD));

    // Next we will get the lists of antenna inputs from each secondary node.
    std::vector<int> antennaInputDisplacements(nodeCount);
    antennaInputDisplacements.at(0) = 0;
    for (std::size_t node = 1; node < nodeCount; ++node) {
        antennaInputDisplacements.at(node) = antennaInputDisplacements.at(node - 1) + antennaInputCounts.at(node - 1);
    }
    auto const totalAntennaInputs = std::accumulate(antennaInputCounts.cbegin(), antennaInputCounts.cend(), 0ull);
    std::vector<unsigned> antennaInputs(totalAntennaInputs);
    // Note that the root node must send data, but actually we don't need anything from root.
    unsigned const dummyRootAntennaInput = 0;
    assertMPISuccess(MPI_Gatherv(&dummyRootAntennaInput, 0, MPI_UNSIGNED, antennaInputs.data(),
        antennaInputCounts.data(), antennaInputDisplacements.data(), MPI_UNSIGNED, 0, MPI_COMM_WORLD));

    // Next we will receive the success status for each antenna input from each secondary node.
    std::vector<char> successes(totalAntennaInputs);    // Can't use bool due to std::vector<bool> specialisation.
    // Note that the root node must send data, but actually we don't need anything from root.
    char const dummyRootSuccess = true;
    assertMPISuccess(MPI_Gatherv(&dummyRootSuccess, 0, MPI_CHAR, successes.data(), antennaInputCounts.data(),
        antennaInputDisplacements.data(), MPI_CHAR, 0, MPI_COMM_WORLD));

    // Next we will receive the number of used channels per antenna input from each secondary node.
    std::vector<unsigned> usedChannelCounts(totalAntennaInputs);
    unsigned const dummyRootUsedChannelCount = 0;
    assertMPISuccess(MPI_Gatherv(&dummyRootUsedChannelCount, 0, MPI_UNSIGNED, usedChannelCounts.data(),
        antennaInputCounts.data(), antennaInputDisplacements.data(), MPI_UNSIGNED, 0, MPI_COMM_WORLD));
    
    // Next we will receive the list of used channels per antenna input from each secondary node.
    std::vector<int> perNodeUsedChannelCounts(nodeCount);
    std::size_t totalUsedChannelCount = 0;
    for (std::size_t node = 1; node < nodeCount; ++node) {
        auto const displacement = antennaInputDisplacements.at(node);
        auto const antennaInputCount = antennaInputCounts.at(node);
        auto const channelCount = std::accumulate(
            usedChannelCounts.cbegin() + displacement,
            usedChannelCounts.cbegin() + displacement + antennaInputCount,
            0u);
        perNodeUsedChannelCounts.at(node) = channelCount;
        totalUsedChannelCount += channelCount;
    }
    std::vector<int> usedChannelDisplacements(nodeCount);
    usedChannelDisplacements.at(0) = 0;
    for (std::size_t node = 1; node < nodeCount; ++node) {
        usedChannelDisplacements.at(node) = usedChannelDisplacements.at(node - 1)
            + perNodeUsedChannelCounts.at(node - 1);
    }
    std::vector<unsigned> usedChannels(totalUsedChannelCount);
    unsigned const dummyRootUsedChannel = 0;
    assertMPISuccess(MPI_Gatherv(&dummyRootUsedChannel, 0, MPI_UNSIGNED, usedChannels.data(),
        perNodeUsedChannelCounts.data(), usedChannelDisplacements.data(), MPI_UNSIGNED, 0, MPI_COMM_WORLD));

    // Finally we combine all the received data.
    std::map<unsigned, ObservationProcessingResults> result;
    auto usedChannelIt = usedChannels.cbegin();
    for (unsigned node = 1; node < nodeCount; ++node) {
        ObservationProcessingResults nodeResults{};
        std::size_t const antennaInputCount = antennaInputCounts.at(node);
        std::size_t const antennaInputDisplacement = antennaInputDisplacements.at(node);
        for (std::size_t antennaInputIdx = 0; antennaInputIdx < antennaInputCount; ++antennaInputIdx) {
            auto const antennaInput = antennaInputs.at(antennaInputDisplacement + antennaInputIdx);
            auto const success = successes.at(antennaInputDisplacement + antennaInputIdx);
            auto const usedChannelCount = usedChannelCounts.at(antennaInputDisplacement + antennaInputIdx);
            nodeResults.results.emplace(
                antennaInput,
                AntennaInputProcessingResults{
                    static_cast<bool>(success),
                    {usedChannelIt, usedChannelIt + usedChannelCount}
                });
            usedChannelIt += usedChannelCount;
        }
        result.emplace(node, std::move(nodeResults));
    }

    return result;
}


SecondaryNodeCommunicator::SecondaryNodeCommunicator(std::shared_ptr<InternodeCommunicationContext> context) :
    InternodeCommunicator{context}
{
    if (getNodeID() == 0) {
        throw std::logic_error{"SecondaryNodeCommunicator cannot be used with node 0."};
    }
}

bool SecondaryNodeCommunicator::receiveAppStartupStatus() const {
    bool status = false;
    assertMPISuccess(MPI_Bcast(&status, 1, MPI_CXX_BOOL, 0, MPI_COMM_WORLD));
    return status;
}

void SecondaryNodeCommunicator::sendNodeSetupStatus(bool status) const {
    char const statusBuffer = status;   // Can't use bool due to std::vector<bool> specialisation.
    assertMPISuccess(MPI_Gather(&statusBuffer, 1, MPI_CHAR, nullptr, 1, MPI_CHAR, 0, MPI_COMM_WORLD));
}

AppConfig SecondaryNodeCommunicator::receiveAppConfig() const {
    // Receive the fixed-size data.
    std::array<unsigned long long, 6> part1Buffer{};
    assertMPISuccess(MPI_Bcast(part1Buffer.data(), part1Buffer.size(), MPI_UNSIGNED_LONG_LONG, 0, MPI_COMM_WORLD));
    auto const [
        observationID,
        signalStartTime,
        ignoreErrors,
        inputDirectoryPathSize,
        invPolyphaseFilterPathSize,
        outputDirectoryPathSize
    ] = part1Buffer;

    // Allocate a buffer for the variable-size data.
    std::vector<char> part2Buffer(inputDirectoryPathSize + invPolyphaseFilterPathSize + outputDirectoryPathSize);
    assertMPISuccess(MPI_Bcast(part2Buffer.data(), part2Buffer.size(), MPI_CHAR, 0, MPI_COMM_WORLD));

    return {
        {part2Buffer.data(), inputDirectoryPathSize},
        static_cast<unsigned>(observationID),
        static_cast<unsigned>(signalStartTime),
        {part2Buffer.data() + inputDirectoryPathSize, invPolyphaseFilterPathSize},
        {part2Buffer.data() + inputDirectoryPathSize + invPolyphaseFilterPathSize, outputDirectoryPathSize},
        static_cast<bool>(ignoreErrors)
    };
}

AntennaConfig SecondaryNodeCommunicator::receiveAntennaConfig() const {
    // First we receive the sizes of the antenna input and frequency channel arrays.
    std::array<unsigned long long, 2> part1Buffer{};
    assertMPISuccess(MPI_Bcast(part1Buffer.data(), part1Buffer.size(), MPI_UNSIGNED_LONG_LONG, 0, MPI_COMM_WORLD));
    auto const [antennaInputCount, channelCount] = part1Buffer;

    // Then we receive the antenna input and frequency channel data.
    std::vector<unsigned> part2Buffer(3 * antennaInputCount + channelCount);
    assertMPISuccess(MPI_Bcast(part2Buffer.data(), part2Buffer.size(), MPI_UNSIGNED, 0, MPI_COMM_WORLD));

    AntennaConfig result{};
    result.antennaInputs.reserve(antennaInputCount);
    for (std::size_t i = 0; i < antennaInputCount; ++i) {
        auto const tile = part2Buffer.at(3 * i);
        auto const signalChain = part2Buffer.at(3 * i + 1);
        auto const flagged = part2Buffer.at(3 * i + 2);
        result.antennaInputs.push_back({tile, static_cast<char>(signalChain), static_cast<bool>(flagged)});
    }
    for (std::size_t i = 0; i < channelCount; ++i) {
        auto const channel = part2Buffer.at(3 * antennaInputCount + i);
        result.frequencyChannels.insert(channel);
    }
    return result;
}

ChannelRemapping SecondaryNodeCommunicator::receiveChannelRemapping() const {
    // Receive the fixed-size data.
    std::array<unsigned, 2> part1Buffer{};
    assertMPISuccess(MPI_Bcast(part1Buffer.data(), part1Buffer.size(), MPI_UNSIGNED, 0, MPI_COMM_WORLD));
    auto const [newSamplingFreq, channelMapSize] = part1Buffer;

    // Receive the variable-size channel map.
    std::vector<unsigned> part2Buffer(channelMapSize * 3);
    assertMPISuccess(MPI_Bcast(part2Buffer.data(), part2Buffer.size(), MPI_UNSIGNED, 0, MPI_COMM_WORLD));

    ChannelRemapping result{newSamplingFreq, {}};
    for (std::size_t i = 0; i < channelMapSize; ++i) {
        auto const oldChannel = part2Buffer.at(3 * i);
        auto const newChannel = part2Buffer.at(3 * i + 1);
        bool const flipped = part2Buffer.at(3 * i + 2);
        result.channelMap.emplace(oldChannel, ChannelRemapping::RemappedChannel{newChannel, flipped});
    }
    return result;
}

std::optional<AntennaInputRange> SecondaryNodeCommunicator::receiveAntennaInputAssignment() const {
    std::array<unsigned, 3> buffer{};
    assertMPISuccess(
        MPI_Recv(buffer.data(), buffer.size(), MPI_UNSIGNED, 0, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE));
    auto const [hasValue, begin, end] = buffer;
    if (hasValue) {
        return AntennaInputRange{begin, end};
    }
    else {
        return std::nullopt;
    }
}

void SecondaryNodeCommunicator::sendProcessingResults(ObservationProcessingResults const& results) const {
    // First we will send the number of antenna inputs to the primary node.
    int const antennaInputCount = results.results.size();
    assertMPISuccess(MPI_Gather(&antennaInputCount, 1, MPI_INT, nullptr, 1, MPI_INT, 0, MPI_COMM_WORLD));

    // Extract the result data into separate arrays that we can send individually.
    std::vector<unsigned> antennaInputs;
    antennaInputs.reserve(antennaInputCount);
    std::vector<char> successes;        // Can't use bool due to std::vector<bool> specialisation.
    successes.reserve(antennaInputCount);
    std::vector<unsigned> usedChannelCounts;
    usedChannelCounts.reserve(antennaInputCount);
    std::vector<unsigned> usedChannels;
    usedChannels.reserve(antennaInputCount * 24ull);        // Approxmiate based on max number of channels.
    for (auto const& [antennaInput, antennaInputResult] : results.results) {
        antennaInputs.push_back(antennaInput);
        successes.push_back(static_cast<char>(antennaInputResult.success));
        usedChannelCounts.push_back(static_cast<unsigned>(antennaInputResult.usedChannels.size()));
        usedChannels.insert(usedChannels.cend(), antennaInputResult.usedChannels.cbegin(),
            antennaInputResult.usedChannels.cend());
    }

    // Next we will send the list of antenna inputs to the primary node.
    assertMPISuccess(MPI_Gatherv(antennaInputs.data(), antennaInputCount, MPI_UNSIGNED, nullptr, nullptr, nullptr,
        MPI_UNSIGNED, 0, MPI_COMM_WORLD));
    
    // Next we will send the success status for each antenna input to the primary node.
    assertMPISuccess(MPI_Gatherv(successes.data(), antennaInputCount, MPI_CHAR, nullptr, nullptr, nullptr, MPI_CHAR, 0,
        MPI_COMM_WORLD));
    
    // Next we will send the number of used channels per antenna input to the primary node.
    assertMPISuccess(MPI_Gatherv(usedChannelCounts.data(), antennaInputCount, MPI_UNSIGNED, nullptr, nullptr, nullptr,
        MPI_CHAR, 0, MPI_COMM_WORLD));

    // Finally we will send the list of used channels per antenna input to the primary node.
    assertMPISuccess(MPI_Gatherv(usedChannels.data(), usedChannels.size(), MPI_UNSIGNED, nullptr, nullptr, nullptr,
        MPI_UNSIGNED, 0, MPI_COMM_WORLD));
}
