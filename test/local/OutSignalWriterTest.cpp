#include "../../src/OutSignalWriter.hpp"
#include "OutSignalWriterTest.hpp"
#include "../TestHelper.hpp"
#include "../../src/Common.hpp"

#include <stdexcept>
#include <cstdint>
#include <vector>
#include <fstream>
#include <string>
#include <filesystem>
#include <iostream>

//input path, obs id, signal start time, poly path, output dir
AppConfig invalidTestConfig = {"123456789",123456789,123456789,"",""};
AppConfig validTestConfig = {"",123456789,123456789,"","/app/test"}; //input and output directories are listed 
//Antena input Physical ID the two components of that struct is the first physical id the second is the char representing what signal chain that id is for 
AntennaInputPhysID testAntenaPhysID = {1,'x'};
std::string filename = validTestConfig.outputDirectoryPath + "/" + std::to_string(validTestConfig.observationID) + "_" + std::to_string(validTestConfig.signalStartTime) + "_" + std::to_string(testAntenaPhysID.tile) + "_signalchain.bin";


OutSignalWriterTest::OutSignalWriterTest() : TestModule{"Output File Writer unit test", {
    {"Valid Appconfig", []() {
        std::vector<std::int16_t> testData = {1,2,3,4,5,6,7,8,9};
        
        outSignalWriter(testData,validTestConfig,testAntenaPhysID);
        
        auto actual = std::filesystem::file_size(filename);
        int expected = testData.size() * sizeof(std::int16_t);
        testAssert(actual == expected);
        
        std::filesystem::remove(filename);
	}},     
    
    {"Invalid Appconfig", []() {
        try {
            std::vector<std::int16_t> testData = {1,2,3,4,5,6,7,8,9};
            outSignalWriter(testData,invalidTestConfig,testAntenaPhysID);
            failTest();
        }
        catch (std::ios::failure const&) {}        
	}},
    
    {"Empty Data input", []() {
        std::vector<std::int16_t> emptyTestData = {};
        outSignalWriter(emptyTestData,validTestConfig,testAntenaPhysID);
        auto actual = std::filesystem::file_size(filename);
        int expected = emptyTestData.size() * sizeof(std::int16_t);
        testAssert(actual == expected);
        std::filesystem::remove(filename);
	}},
    
    {"File already exists (double call to writer function)", []() {
        try {
            std::vector<std::int16_t> testData = {1,2,3,4,5,6,7,8,9};
            outSignalWriter(testData,invalidTestConfig,testAntenaPhysID);
            outSignalWriter(testData,invalidTestConfig,testAntenaPhysID);
            failTest();
        }
        catch (std::ios::failure const&) {}
        std::filesystem::remove(filename);        
	}}            

}} {}