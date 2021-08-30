#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <filesystem>

struct AppConfig;
struct AntennaInputPhysID;

//this fuction throws the following exception throw std::ios_base::failure for IO related failures during runtime
// std::system_error::runtime_error is used for any non IO related errors that are encountered when consturction file strings

void outSignalWriter(const std::vector<std::int16_t> &inputData, const AppConfig &observation, const AntennaInputPhysID &physID);

std::filesystem::path generateFilePath(const AppConfig &observation, const AntennaInputPhysID &physID);

class outSignalException : public std::exception{  
    public:  
        const char * what() const throw()  
        {  
            return "Output File Error";  
        }  
};  