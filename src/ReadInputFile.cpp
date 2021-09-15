#include "ReadInputFile.hpp"
#include "Common.hpp"
#include <complex>
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <filesystem>
#include <set>

//where dose this random ass number come from
//MWA documentation states this is the standered file size for a 128 tile dual polarisation file
const long long stdinputsize = 5275652096;

//Main function for reading in of the data file takes the name of the file it is to read from
//will read all files for a specific calculation into 1 complex vector array for signal processing
std::vector<std::complex<float> > readInputDataFile(std::string fileName,int antenaInput){    
    fileName = "mnt/1294797712_1294797712_118.sub";
    
    //this needs to be moved to a diff function as for each file it could be a different number
    long long Ninputs = 256;
    long long DELAYDATALENGTH = 128000*Ninputs;


    std::string path = "mnt";
    std::vector<std::filesystem::path> allfiles;

    for (const auto & file : std::filesystem::directory_iterator(path))
         allfiles.push_back(file.path()); 


    for (int i = 0; i < allfiles.size(); i++) {
    std::cout<< std::fixed << allfiles.at(i) << ' ';
    }


    std::vector<std::complex<float> > datavalues;
    //Opening the first data filestream this changes each interation of the loop to pass thru all files
    std::ifstream datafile(fileName, std::ios::binary);   

    // main error handling statement
    if(datafile.is_open()){
        //error checking to make sure the file is of the right size this is to validate that all the infomation inside atleast of the correct
        validateInputData(fileName);
        
        //this gets changed depending on what antean we want to read the data from
        //per antena per polarisation there is a 64000 bytes of data        
        long long offset;
        if(antenaInput != 0){   
            offset = 64000*antenaInput;
        }
        else{
            offset = 0;
        }
        
        //reading the data into the vector
        //known size of data file enteries as per file specification pre allocation to save time later
        datavalues.reserve(64000*sizeof(std::complex<float>));        
        //alot of this is dependent on the meta data file reader numbers are subject to change once i figure out what to do
        //seeking to the start of the data portion of the file 
        //this will be antena 0 polarisation x and y sample 1 of 64000
        datafile.seekg(4096+offset, std::ios::beg);
        for(int i = 1; i <= 160;i++){
            datafile.seekg(32768000, std::ios::cur);
            for(int j = 1; j<=64000;j++){
                int16_t buffer;
                datafile.read(reinterpret_cast<char*>(&buffer),2*sizeof(signed char));
                signed char rbuffer = buffer & 0xFF;
                signed char ibuffer = buffer >> 8;
                datavalues.push_back({rbuffer,ibuffer});
            }
        }
    
    }
    else{
        //if file was unable to be opened an exception will be thrown
        throw ReadInputDataException("Failed to open the file");
    }

    
    std::cout.precision(2);
    std::cout << "file pointer location"<< std::endl;    
    std::cout << datafile.tellg() << std::endl;
    datafile.seekg(0,std::ios::beg);
    std::cout << "total enteries in data values" << std::endl;    
    std::cout << std::to_string(datavalues.size()) << std::endl;
    /*
    for (int i = 0; i < datavalues.size(); i++) {
    std::cout<< std::fixed << datavalues.at(i) << ' ';
    }
*/


    return datavalues;
}
//function used to validate if the data file is the correct size and thus allowing the program to know if there is anything missing.
//The file size this program will be given is a constant as such its easy to validate if the file is correct or not
//break
bool validateInputData(std::string fileName){
    if(std::filesystem::file_size(fileName) != stdinputsize){
        throw ReadInputDataException("Error File size is not expected");
    }    
    else{
        return true;
    }
}