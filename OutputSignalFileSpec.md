# Specification of the Output Signal Files

These files are where the application stores the reconstructed time domain signal.
Each file corresponds to one RF input from one antenna tile.

## Naming Scheme

The files are named by the following scheme:  
`<observationID>_<startTime>_<tileID>_<signalChain>.bin`

`<observationID>` is the ID of the processed observation (i.e. the GPS time of the start of the observation).  
`<startTime>` is the GPS time within the observation that the signal starts at.  
`<tileID>` is the physical ID of the tile.  
`<signalChain>` is an identifier for the signal chain (typically `X` or `Y`).

## File Structure

The file is a binary file; values in the file are encoded as their raw binary values.

The file consists of a 1D array of real numbers. The dimension of the array is time.
Each element of the array is a single real number, a 16-bit two's complement signed integer, stored in little-endian byte order.

All data is stored contiguously in the file with no padding between values.

## Notes

The period in between time samples, given by the sampling rate, depends on the frequency channel configuration.
This is a result of the application losslessly downsampling the reconstructed time domain signal.
One may determine the sampling rate by inspecting the output log file generated by the application.
All the output signals produced by a single run of the application will have that sampling frequency (i.e. it is fixed across all tiles / RF inputs).
