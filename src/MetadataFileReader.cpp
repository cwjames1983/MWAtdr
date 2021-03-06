#include "MetadataFileReader.hpp"

#include <cstring>
#include <filesystem>
#include <regex>

// Constructor which creates internal MetafitsMetadata (based on VoltageContext)
MetadataFileReader::MetadataFileReader(AppConfig const& appConfig) {
	// Verify metafits file exists at specified input directory path (and is not empty)
	validateMetafits(appConfig);
	// Verify which voltage files exist at specified input directory path
	auto const voltageFiles = findVoltageFiles(appConfig);
	unsigned const numFiles = voltageFiles.size();
	if (numFiles == 0) {
		throw MetadataException("Invalid/no voltage files at specified path");
	}
	// Copy string vector to const char* (c string) vector for creating VoltageContext
	std::vector<const char*> voltageFilenames;
	for (unsigned i = 0; i < numFiles; i++) {
		voltageFilenames.push_back(voltageFiles.at(i).c_str());
	}
	// Create VoltageContext for use in MetafitsMetadata creation
	std::string metafitsFilename = appConfig.inputDirectoryPath +
	                               std::to_string(appConfig.observationID) + ".metafits";
    const unsigned ERROR_MESSAGE_LENGTH = 1024;
	if (mwalib_voltage_context_new(metafitsFilename.c_str(), voltageFilenames.data(), numFiles, &voltageContext,
	                               nullptr, ERROR_MESSAGE_LENGTH) != EXIT_SUCCESS) {
		throw MetadataException("Error creating VoltageContext");
	}
	// Create MetafitsMetadata from VoltageContext
	if (mwalib_metafits_metadata_get(nullptr, nullptr, voltageContext, &metafitsMetadata,
	                                 nullptr, ERROR_MESSAGE_LENGTH) != EXIT_SUCCESS) {
		throw MetadataException("Error loading metadata");
    }
}

// Finds all of the observation signal files within the specified input directory path
std::vector<std::string> MetadataFileReader::findVoltageFiles(AppConfig const& appConfig) {
	std::vector<std::string> voltageFilenames;
    std::regex target (std::to_string(appConfig.observationID) + "_" +
	                   std::to_string(appConfig.signalStartTime) + "_" + "[0-9]+");
	try {
		// Detect and store valid voltage data file paths
		for (auto const& file : std::filesystem::directory_iterator(appConfig.inputDirectoryPath)) {
			std::filesystem::path path (file.path());
			// Add (non-empty) paths that match the file format:
			// inputDirectoryPath/observationID_signalStartTime_channel.sub
			if (path.extension() == ".sub") {
				if (std::regex_match((std::string) path.stem(), target)) {
					if (!std::filesystem::is_empty(path)) {
				        voltageFilenames.push_back(path);
					}
				}
			}
		}
	}
	catch (std::filesystem::filesystem_error const&) {
		throw MetadataException("Error opening voltage file (no such file or directory)");
	}
	return voltageFilenames;
}

// Checks if metafits file exists and is valid (not-empty)
void MetadataFileReader::validateMetafits(AppConfig const& appConfig) {
	std::filesystem::path dir (appConfig.inputDirectoryPath);
	std::filesystem::path file (std::to_string(appConfig.observationID) + ".metafits");
	std::filesystem::path metafits = dir / file;

	if (!std::filesystem::exists(metafits)) {
		throw MetadataException("No metafits at specified path");
	}
	if (std::filesystem::is_empty(metafits)) {
		throw MetadataException("Invalid metafits at specified path");
	}
}


// Gathers antenna configuration from the metadata
AntennaConfig MetadataFileReader::getAntennaConfig(AppConfig const& appConfig) {
    AntennaConfig config;
	// Storing the logical to physical antenna mappings
    config.antennaInputs = getPhysicalAntennaInputs();
	// Storing the frequency channels recorded in the observation
	config.frequencyChannels = getAvailableFrequencyChannelsUsed(appConfig);
	return config;
}

std::vector<AntennaInputPhysID> MetadataFileReader::getPhysicalAntennaInputs() {
	std::vector<AntennaInputPhysID> antennaInputs;
	// Convert each Rfinput to AntennaInputPhysID and add to the vector
	for (unsigned i = 0; i < metafitsMetadata->num_rf_inputs; i++) {
        auto const antenna = metafitsMetadata->rf_inputs[i];
		antennaInputs.push_back({antenna.tile_id, *(antenna.pol), antenna.flagged});
	}
	return antennaInputs;
}

std::set<unsigned> MetadataFileReader::getAvailableFrequencyChannelsUsed(AppConfig const& appConfig) {
	std::set<unsigned> frequencyChannels;
	// Add each frequency channel number whose file is present in input directory
	auto const voltageFiles = findVoltageFiles(appConfig);
	for (auto i : voltageFiles) {
		auto const channel = std::stoul(i.substr(i.find_last_of("_") + 1, (i.find_last_of(".")) - (i.find_last_of("_") + 1)), nullptr, 0);
		frequencyChannels.insert(channel);
	}
	return frequencyChannels;
}


std::set<unsigned> MetadataFileReader::getFrequencyChannels() {
    std::set<unsigned> frequencyChannels;

    // Add each frequency channel number recorded in the observation
    for (unsigned i = 0; i < metafitsMetadata->num_metafits_coarse_chans; i++) {
        frequencyChannels.insert(metafitsMetadata->metafits_coarse_chans[i].rec_chan_number);
    }
    return frequencyChannels;
}


MetadataFileReader::~MetadataFileReader() {
	freeMetadata();
}

void MetadataFileReader::freeMetadata() {
	mwalib_metafits_metadata_free(metafitsMetadata);
    mwalib_voltage_context_free(voltageContext);
}