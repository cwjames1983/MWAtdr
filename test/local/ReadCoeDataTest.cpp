#include "../../src/ReadCoeData.hpp"
#include "ReadCoeDataTest.hpp"
#include "../TestHelper.hpp"
#include "../../src/Common.hpp"

#include <stdexcept>
#include <cstdint>
#include <vector>
#include <fstream>
#include <string>
#include <filesystem>
#include <iostream>


std::vector<std::complex<float>> readCoeData(std::string fileName);

std::string filename = "coeficentdataFile.bin";
std::string incorectDataFile = "badDatacoeficentdataFile";

ReadCoeDataTest::ReadCoeDataTest() : TestModule{"Read Coeficent data Test", {
    
    {"Valid InputFile", []() {
       std::vector<std::complex<float>> actual = readCoeData("coeficentdataFile.bin");

        testAssert(actual.size() % 256 == 0);
	}},

    {"Invalid InputFile(wrong size of data inside the file)", []() {
       try{
           readCoeData(incorectDataFile);
           failTest();
       }
       catch(std::ios::failure const&){}
	}},
    
    {"Valid InputFile(Data Integrity Check)", []() {
       std::vector<std::complex<float>> actual = readCoeData("coeficentdataFile.bin");
       testAssert(std::adjacent_find(actual.begin(), actual.end(), std::not_equal_to<>() ) == actual.end() == true);
	}},    

    {"No Data File", []() {
       try{
           readCoeData("11");
           failTest();
       }
       catch(std::ios::failure const&){}
	}}

}} {}