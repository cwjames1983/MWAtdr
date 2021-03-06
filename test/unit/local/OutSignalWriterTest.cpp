#include "OutSignalWriter.hpp"
#include "OutSignalWriterTest.hpp"
#include "TestHelper.hpp"
#include "Common.hpp"

#include <stdexcept>
#include <cstdint>
#include <vector>
#include <fstream>
#include <string>
#include <filesystem>
#include <iostream>
#include <memory>

//input path, obs id, signal start time, poly path, output dir
AppConfig invalidTestConfig = {"123456789",123456789,123456789,"",""};
AppConfig validTestConfig = {"",123456789,123456789,"","/tmp/"}; //input and output directories are listed 
//Antena input Physical ID the two components of that struct is the first physical id the second is the char representing what signal chain that id is for 
AntennaInputPhysID testAntenaPhysID = {1,'x'};

std::filesystem::path filename = validTestConfig.outputDirectoryPath + std::to_string(validTestConfig.observationID) + "_" + std::to_string(validTestConfig.signalStartTime) + "_" + std::to_string(testAntenaPhysID.tile) +"_"+ std::string(1,testAntenaPhysID.signalChain)+".bin";


class OutSignalWriterTest : public StatelessTestModuleImpl {
public:
    OutSignalWriterTest();
};


OutSignalWriterTest::OutSignalWriterTest() : StatelessTestModuleImpl{{
    {"Valid Appconfig", []() {
        std::vector<std::int16_t> testData = {1,2,3,4,5,6,7,8,9};
        outSignalWriter(testData,validTestConfig,testAntenaPhysID);
        auto actual = std::filesystem::file_size(filename);
        int expected = testData.size() * sizeof(std::int16_t);
        testAssert(actual == expected);
        std::filesystem::remove(filename);
	}},     
    
    {"Empty File directory string", []() {
        try {
            std::vector<std::int16_t> testData = {1,2,3,4,5,6,7,8,9};
            outSignalWriter(testData,invalidTestConfig,testAntenaPhysID);
            failTest();
        }
        catch(OutSignalException const&){}     
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
        std::vector<std::int16_t> testData = {1,2,3,4,5,6,7,8,9};
        try {
            outSignalWriter(testData,validTestConfig,testAntenaPhysID);
        }
        catch (OutSignalException const& e){}

        if(std::filesystem::exists(filename)){
            try{
                outSignalWriter(testData,validTestConfig,testAntenaPhysID);
            }
            catch(OutSignalException const& e){}
        }
        else{
            failTest();
        }
        std::filesystem::remove(filename);        
	}},
    
    {"Validate Output data is correct", []() {
        std::vector<std::int16_t> testData = {1,2,3,4,5,6,7,8,9};
        std::vector<std::int16_t> actual;
        std::int16_t data;
        try {
            outSignalWriter(testData,validTestConfig,testAntenaPhysID);
        }
        catch (OutSignalException const& e){}
        std::ifstream validatefile(filename);
        while(validatefile.read(reinterpret_cast<char*>(&data), sizeof(int16_t)))
        actual.push_back(data);
        
        testAssert(testData == actual);
        std::filesystem::remove(filename);        
	}}, 
    {"Validate File Name is being created correctly", []() {
        std::vector<std::int16_t> testData = {1,2,3,4,5,6,7,8,9};
        try {
            outSignalWriter(testData,validTestConfig,testAntenaPhysID);
        }
        catch (OutSignalException const& e){}
        testAssert(std::filesystem::exists("/tmp/123456789_123456789_1_x.bin") == true);
        std::filesystem::remove("/tmp/123456789_123456789_1_x.bin");    
	
    }},                

}} {}


TestModule outSignalWriterTest() {
    return {
        "Output File Writer unit test",
        []() { return std::make_unique<OutSignalWriterTest>(); }
    };
}
