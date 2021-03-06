#include "InternodeCommunicationTest.hpp"

#include <chrono>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <thread>
#include <utility>

#include "ChannelRemapping.hpp"
#include "Common.hpp"
#include "InternodeCommunication.hpp"
#include "NodeAntennaInputAssigner.hpp"
#include "TestHelper.hpp"


class InternodeCommunicationTest : public StatelessTestModuleImpl {
public:
    InternodeCommunicationTest(PrimaryNodeCommunicator communicator);
    InternodeCommunicationTest(SecondaryNodeCommunicator communicator);
};


InternodeCommunicationTest::InternodeCommunicationTest(PrimaryNodeCommunicator communicator) : StatelessTestModuleImpl{{
    // The order of test cases must match the order of the secondary node test cases, otherwise deadlock can occur.
        
    {"Repeat internode communication initialisation", []() {
        try {
            InternodeCommunicationContext::initialise();
            failTest();
        }
        catch (std::logic_error const&) {}
    }},

    {"Got right communicator subclass", [communicator]() {
        testAssert(communicator.getNodeID() == 0);
    }},

    {"Instantiating wrong communicator class", [communicator]() {
        try {
            SecondaryNodeCommunicator{communicator.getContext()};
            failTest();
        }
        catch (std::logic_error const&) {}
    }},

    {"sendAppStartupStatus(false)", [communicator]() {
        communicator.sendAppStartupStatus(false);
    }},

    {"sendAppStartupStatus(true)", [communicator]() {
        communicator.sendAppStartupStatus(true);
    }},

    {"receiveNodeSetupStatus()", [communicator]() {
        auto const actual = communicator.receiveNodeSetupStatus();
        std::map<unsigned, bool> expected;
        auto const nodeCount = communicator.getNodeCount();
        for (unsigned node = 1; node < nodeCount; ++node) {
            expected.emplace(node, node % 2 == 0);
        }
        testAssert(actual == expected);
    }},

    {"sendAppConfig()", [communicator]() {
        AppConfig const appConfig{
            "/group/mwavcs/myObservation",
            1000000000,
            1000000016,
            "/group/mwavcs/inversePolyphaseFilter.bin",
            "/group/mwavcs/myProcessedObservation",
            true
        };
        communicator.sendAppConfig(appConfig);
    }},

    {"sendAntennaInputAssignment()", [communicator]() {
        auto const nodeCount = communicator.getNodeCount();
        for (unsigned node = 1; node < nodeCount; ++node) {
            std::optional<AntennaInputRange> assignment;
            if (node % 4 != 1) {
                assignment = {7 * node, 7 * node + 4};
            }
            communicator.sendAntennaInputAssignment(node, assignment);
        }
    }},

    {"sendAntennaInputAssignment() to primary node", [communicator]() {
        try {
            communicator.sendAntennaInputAssignment(0, std::nullopt);
            failTest();
        }
        catch (std::invalid_argument const&) {}
    }},

    {"sendChannelRemapping()", [communicator]() {
        ChannelRemapping const channelRemapping{
            14,
            {
                {7, {7, false}},
                {18, {4, false}},
                {47, {5, false}},
                {53, {3, true}},
                {71, {1, false}}
            }
        };
        communicator.sendChannelRemapping(channelRemapping);
    }},

    {"sendAntennaConfig()", [communicator]() {
        AntennaConfig const antennaConfig{
            {
                {0, 'X', false}, {0, 'Y', true}, {1, 'X', true}, {1, 'Y', false},
                {2, 'X', true}, {3, 'Y', false}, {76, 'Y', false}, {76, 'X', false}
            },
            {0, 3, 7, 4, 87, 231}
        };
        communicator.sendAntennaConfig(antennaConfig);
    }},

    {"receiveProcessingResults()", [communicator]() {
        auto const nodeCount = communicator.getNodeCount();
        auto const actual = communicator.receiveProcessingResults();
        std::map<unsigned, ObservationProcessingResults> expected;
        for (unsigned node = 1; node < nodeCount; ++node) {
            ObservationProcessingResults nodeResults;
            // Trying to get a mix of data values and sizes here, hence the modulos and xors and odd numbers.
            if (node % 4 != 1) {
                unsigned const antennaInputBegin = node * 17;
                unsigned const antennaInputEnd = node * 17 + 13;
                for (unsigned antennaInput = antennaInputBegin; antennaInput <= antennaInputEnd; ++antennaInput) {
                    AntennaInputProcessingResults antennaInputResults{
                        (node ^ antennaInput) % 3 == 2,
                        {}
                    };
                    if (node % 4 != 2) {
                        for (unsigned channel = 92; channel <= 120; ++channel) {
                            if ((node * antennaInput * channel) % 5 == 3) {
                                antennaInputResults.usedChannels.insert(channel);
                            }
                        }
                    }
                    nodeResults.results.emplace(antennaInput, std::move(antennaInputResults));
                }
            }
            expected.emplace(node, std::move(nodeResults));
        }
        testAssert(actual == expected);
    }},

    {"Error communication - no error", [communicator]() {
        auto const nodeID = communicator.getNodeID();
        auto const iterations = 500000ul + (nodeID * 100000ul);
        communicator.synchronise();
        for (unsigned long i = 0; i < iterations; ++i) {
            testAssert(!communicator.getErrorStatus());
        }
    }},

    {"Error communication - 1 error occurs", [communicator]() mutable {
        // Unfortunately this test is dodgy because it's impossible to ensure that all nodes receive the error
        // status without synchronising. But we don't want to synchronise, because in real usage an error would
        // occur during a time where there is no synchronisation between nodes (the signal processing stage).
        // All we can do is try to make the loop long enough that an error is indicated while all the nodes are
        // probably still in the loop.

        auto const nodeID = communicator.getNodeID();
        auto const nodeCount = communicator.getNodeCount();
        // Nodes will take turns being the one to indicate an error.
        for (unsigned i = 0; i < nodeCount; ++i) {
            bool gotError = false;
            communicator.synchronise();
            for (unsigned long j = 0; j < 10000; ++j) {
                // Sleep for a somewhat random amount of time to randomise the point at which an error is indicated.
                std::this_thread::sleep_for(std::chrono::microseconds{(nodeID * 74523) % 1000});
                if (j == 5000 && i == nodeID) {
                    communicator.indicateError();
                }
                if (communicator.getErrorStatus()) {
                    gotError = true;
                    break;
                }
            }
            testAssert(gotError);
        }
    }},

    {"Error communication - many errors occur", [communicator]() mutable {
        // Similar to the test above, this test is kinda dodgy due to uncertain timing.

        auto const nodeID = communicator.getNodeID();
        bool gotError = false;
        communicator.synchronise();
        for (unsigned long i = 0; i < 10000; ++i) {
            // Sleep for a somewhat random amount of time to randomise the point at which an error is indicated.
            std::this_thread::sleep_for(std::chrono::microseconds{(nodeID * 123475) % 1000});
            if (i == 5000) {
                communicator.indicateError();
            }
            if (communicator.getErrorStatus()) {
                gotError = true;
                break;
            }
        }
        testAssert(gotError);
    }}
}}
{}


InternodeCommunicationTest::InternodeCommunicationTest(SecondaryNodeCommunicator communicator) : StatelessTestModuleImpl{{
    // The order of test cases must match the order of the primary node test cases, otherwise deadlock can occur.

    {"Repeat internode communication initialisation", []() {
        try {
            InternodeCommunicationContext::initialise();
            failTest();
        }
        catch (std::logic_error const&) {}
    }},

    {"Got right communicator subclass", [communicator]() {
        testAssert(communicator.getNodeID() > 0);
    }},

    {"Instantiating wrong communicator class", [communicator]() {
        try {
            PrimaryNodeCommunicator{communicator.getContext()};
            failTest();
        }
        catch (std::logic_error const&) {}
    }},

    {"receiveAppStartupStatus(false)", [communicator]() {
        auto const actual = communicator.receiveAppStartupStatus();
        auto const expected = false;
        testAssert(actual == expected);
    }},

    {"receiveAppStartupStatus(true)", [communicator]() {
        auto const actual = communicator.receiveAppStartupStatus();
        auto const expected = true;
        testAssert(actual == expected);
    }},

    {"sendNodeSetupStatus()", [communicator]() {
        auto const nodeID = communicator.getNodeID();
        communicator.sendNodeSetupStatus(nodeID % 2 == 0);
    }},

    {"receiveAppConfig()", [communicator]() {
        auto const actual = communicator.receiveAppConfig();
        AppConfig const expected{
            "/group/mwavcs/myObservation",
            1000000000,
            1000000016,
            "/group/mwavcs/inversePolyphaseFilter.bin",
            "/group/mwavcs/myProcessedObservation",
            true
        };
        testAssert(actual == expected);
    }},

    {"receiveAntennaInputAssignment()", [communicator]() {
        auto const nodeID = communicator.getNodeID();
        auto const actual = communicator.receiveAntennaInputAssignment();
        std::optional<AntennaInputRange> expected;
        if (nodeID % 4 != 1) {
            expected = {7 * nodeID, 7 * nodeID + 4};
        }
        testAssert(actual == expected);
    }},

    {"receiveChannelRemapping()", [communicator]() {
        auto const actual = communicator.receiveChannelRemapping();
        ChannelRemapping const expected{
            14,
            {
                {7, {7, false}},
                {18, {4, false}},
                {47, {5, false}},
                {53, {3, true}},
                {71, {1, false}}
            }
        };
        testAssert(actual == expected);
    }},

    {"receiveAntennaConfig()", [communicator]() {
        auto const actual = communicator.receiveAntennaConfig();
        AntennaConfig const expected{
            {
                {0, 'X', false}, {0, 'Y', true}, {1, 'X', true}, {1, 'Y', false},
                {2, 'X', true}, {3, 'Y', false}, {76, 'Y', false}, {76, 'X', false}
            },
            {0, 3, 7, 4, 87, 231}
        };
        testAssert(actual == expected);
    }},

    {"sendProcessingResults()", [communicator]() {
        auto const nodeID = communicator.getNodeID();
        ObservationProcessingResults processingResults{};
        // Trying to get a mix of data values and sizes here, hence the modulos and xors and odd numbers.
        if (nodeID % 4 != 1) {
            unsigned const antennaInputBegin = nodeID * 17;
            unsigned const antennaInputEnd = nodeID * 17 + 13;
            for (unsigned antennaInput = antennaInputBegin; antennaInput <= antennaInputEnd; ++antennaInput) {
                AntennaInputProcessingResults antennaInputResults{
                    (nodeID ^ antennaInput) % 3 == 2,
                    {}
                };
                if (nodeID % 4 != 2) {
                    for (unsigned channel = 92; channel <= 120; ++channel) {
                        if ((nodeID * antennaInput * channel) % 5 == 3) {
                            antennaInputResults.usedChannels.insert(channel);
                        }
                    }
                }
                processingResults.results.emplace(antennaInput, std::move(antennaInputResults));
            }
        }
        communicator.sendProcessingResults(processingResults);
    }},

    {"Error communication - no error", [communicator]() {
        auto const nodeID = communicator.getNodeID();
        auto const iterations = 500000ul + (nodeID * 100000ul);
        communicator.synchronise();
        for (unsigned long i = 0; i < iterations; ++i) {
            testAssert(!communicator.getErrorStatus());
        }
    }},

    {"Error communication - 1 error occurs", [communicator]() mutable {
        // Unfortunately this test is dodgy because it's impossible to ensure that all nodes receive the error
        // status without synchronising. But we don't want to synchronise, because in real usage an error would
        // occur during a time where there is no synchronisation between nodes (the signal processing stage).
        // All we can do is try to make the loop long enough that an error is indicated while all the nodes are
        // probably still in the loop.

        auto const nodeID = communicator.getNodeID();
        auto const nodeCount = communicator.getNodeCount();
        // Nodes will take turns being the one to indicate an error.
        for (unsigned i = 0; i < nodeCount; ++i) {
            bool gotError = false;
            communicator.synchronise();
            for (unsigned long j = 0; j < 10000; ++j) {
                // Sleep for a somewhat random amount of time to randomise the point at which an error is indicated.
                std::this_thread::sleep_for(std::chrono::microseconds{(nodeID * 74523) % 1000});
                if (j == 5000 && i == nodeID) {
                    communicator.indicateError();
                }
                if (communicator.getErrorStatus()) {
                    gotError = true;
                    break;
                }
            }
            testAssert(gotError);
        }
    }},

    {"Error communication - many errors occur", [communicator]() mutable {
        // Similar to the test above, this test is kinda dodgy due to uncertain timing.

        auto const nodeID = communicator.getNodeID();
        bool gotError = false;
        communicator.synchronise();
        for (unsigned long i = 0; i < 10000; ++i) {
            // Sleep for a somewhat random amount of time to randomise the point at which an error is indicated.
            std::this_thread::sleep_for(std::chrono::microseconds{(nodeID * 123475) % 1000});
            if (i == 5000) {
                communicator.indicateError();
            }
            if (communicator.getErrorStatus()) {
                gotError = true;
                break;
            }
        }
        testAssert(gotError);
    }}
}}
{}


TestModule internodeCommunicationTest(PrimaryNodeCommunicator communicator) {
    return {
        "Internode communication module unit test (primary node)",
        [&communicator]() { return std::make_unique<InternodeCommunicationTest>(communicator); }
    };
}

TestModule internodeCommunicationTest(SecondaryNodeCommunicator communicator) {
    return {
        "Internode communication module unit test (secondary node)",
        [&communicator]() { return std::make_unique<InternodeCommunicationTest>(communicator); }
    };
}
