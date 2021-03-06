#pragma once

#include "Common.hpp"
#include "mwalib.h"

#include <set>
#include <stdexcept>
#include <string>
#include <vector>

// MetadataFileReader constructor throws MetadataException
class MetadataFileReader {
	private:
	    VoltageContext* voltageContext;
	    MetafitsMetadata* metafitsMetadata;

        void validateMetafits(AppConfig const& appConfig);
        std::vector<std::string> findVoltageFiles(AppConfig const& appConfig);
	    std::vector<AntennaInputPhysID> getPhysicalAntennaInputs();
        std::set<unsigned> getAvailableFrequencyChannelsUsed(AppConfig const& appConfig);
		void freeMetadata();

	public:
	    MetadataFileReader(AppConfig const& appConfig);
        AntennaConfig getAntennaConfig(AppConfig const& appConfig);
		std::set<unsigned> getFrequencyChannels();
		~MetadataFileReader();
};

class MetadataException : public std::runtime_error {
	public:
	    MetadataException(const std::string& message) : std::runtime_error(message) {}
};