#include "ChannelRemapping.hpp"

#include <stdexcept>
#include <utility>


// Aliases a channel using a given Nyquist frequency.
static ChannelRemapping::RemappedChannel aliasChannel(unsigned channel, unsigned nyquistFreq) {
    auto const samplingFreq = 2 * nyquistFreq;
    auto const diff = channel % samplingFreq;
    if (diff <= nyquistFreq) {
        return {diff, false};
    }
    else {
        return {samplingFreq - diff, true};
    }
}


ChannelRemapping computeChannelRemapping(unsigned samplingFreq, std::set<unsigned> const& channels) {
    if (samplingFreq % 2 != 0) {
        throw std::invalid_argument{"samplingFreq must be even"};
    }
    for (auto const channel : channels) {
        if (channel > samplingFreq / 2) {
            throw std::invalid_argument{"Frequency channels must be <= samplingFreq/2"};
        }
    }

    // Edge cases which cause trouble for the general algorithm.
    if (channels.size() == 0) {
        return {samplingFreq, {}};
    }
    else if (channels.size() == 1) {
        auto const channel = *channels.cbegin();
        // TODO? If there is only channel 0, then what should the new sampling frequency be?
        // Don't think it can be 0, and probably 1 is invalid because it's an odd number.
        auto const newSamplingFreq = 2 * std::max(channel, 1u);
        return {newSamplingFreq, {{channel, {0, false}}}};
    }
    else {
        // If we have N channels, then we need at very least we need N new channels <= the new Nyquist frequency.
        unsigned const minNyquistFreq = channels.size() - 1;

        unsigned newNyquistFreq;
        std::map<unsigned, ChannelRemapping::RemappedChannel> remapping;
        std::set<unsigned> newChannels;
        for (newNyquistFreq = minNyquistFreq; ; newNyquistFreq += 1) {
            remapping.clear();
            newChannels.clear();

            bool valid = true;
            for (auto const channel : channels) {
                auto const alias = aliasChannel(channel, newNyquistFreq);
                // TODO: handle remapping to 0 or Nyquist frequency, which seems to be broken? Follow up with Clancy.
                if (newChannels.count(alias.newChannel) > 0) {
                    // Alias is already filled by another channel.
                    valid = false;
                    break;
                }
                else {
                    newChannels.insert(alias.newChannel);
                    remapping[channel] = alias;
                }
            }

            // Since we are exploring new sampling frequencies from low to high, the first valid remapping is optimal.
            if (valid) {
                break;
            }
        }

        auto const newSamplingFreq = 2 * newNyquistFreq;
        return {newSamplingFreq, std::move(remapping)};
    }
}


bool operator==(ChannelRemapping const& lhs, ChannelRemapping const& rhs) {
    return lhs.newSamplingFreq == rhs.newSamplingFreq && lhs.channelMap == rhs.channelMap;
}

bool operator==(ChannelRemapping::RemappedChannel const& lhs, ChannelRemapping::RemappedChannel const& rhs) {
    return lhs.newChannel == rhs.newChannel && lhs.flipped == rhs.flipped;
}
