#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <filesystem>

struct AppConfig;
struct AntennaInputPhysID;

//This function throws the following exception outSignalExcpetion that can be caught and ther error string being used with e.getMessage() to get the error message from the exception object

void outSignalWriter(const std::vector<std::int16_t> &inputData, const AppConfig &observation, const AntennaInputPhysID &physID);

//Custom Exception
class OutSignalException : public std::exception {
public:
   OutSignalException(const std::string& msg) : msg_(msg) {}
  ~OutSignalException() {}

   std::string getMessage() const {return(msg_);}
private:
   std::string msg_;
};
