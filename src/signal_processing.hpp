#pragma once
#include<complex>
#include<vector>
#include<map>
#include"channel_remapping.hpp"

// Function responsible for all the transoformations, filters and downsampling
// the signal data.
std::vector<short> processSignal(std::vector<std::complex<float>> signalData,
                               std::vector<std::complex<float>> coefficiantPFB,
                               ChannelRemapping remappingData);
// Remaps the channels based on the mapping given to it, this mapping is to be generated
// by computeChannelRemapping() in channel_remapping.hpp
void remapChannels(std::vector<std::complex<float>> signalData,
                   std::map<unsigned, unsigned> const mapping);

// Performs an inverse polyphase filter bank (PFB) on the signal data, the mapping is required
// for this function so the convolution only goes over the appropriate channels. This mapping
// is provided by computeChannelRemapping() in channel_rempping.hpp
void performPFB(std::vector<std::complex<float>> signalData,
                std::vector<std::complex<float>> coefficantPFB,
                std::map<unsigned, unsigned> const mapping);

// Performs an inverse discrete fourier transorm on the signal data, changing the frequency
// data into a time domain signal
std::vector<int> performDFT(std::vector<std::complex<float>> signalData);


// Downsamples data based on the new sampling rate. This sample rate should be generated by
// computeChannelRemapping()_in channel_remapping.hpp
void downsampleData(std::vector<int> signalData, unsigned samplingRate);

// Converts the downsampled time domain array into a 16bit signed int with clamping
std::vector<short> doPostProcessing(std::vector<int> signalData);


