# Specification of the Inverse Polyphase Filter File

This file contains the real-domain filter used to invert the polyphase filterbank.
The filter is convolved with the frequency domain signal data on a per-frequency-channel basis.

## File Structure

The file is a binary file; values in the file are encoded as their raw binary values.

The file starts with an 8-bit unsigned integer, *filterLength*, specifying the length of the filter in the time dimension.
The software is guaranteed to support any *filterLength* values in the range [1, 128].

The file then contains a 2D array of filter coefficients.
The outer dimension of the array is time, of size *filterLength*.
The inner dimension of the array is frequency channel, of fixed size 256.  
Each coefficient is a single real number, a IEEE-754 32-bit floating point value, stored in little-endian byte order.

All data is stored contiguously in the file with no padding between values.
Thus the length of the file is: `1 + filterLength * 256 * 4`.

## Example

Example data layout for *filterLength*=16:

    Byte 0: 16
    Bytes 1-4: time 0 channel 0 coefficient
    Bytes 5-8: time 0 channel 1 coefficient
    ...
    Bytes 1021-1024: time 0 channel 255 coefficient
    Bytes 1025-1028: time 1 channel 0 coefficient
    Bytes 1029-1032: time 1 channel 1 coefficient
    ...
    Bytes 2045-2048: time 1 channel 255 coefficient
    ...
    Bytes 16381-16384: time 15 channel 255 coefficient

It is evident that the coefficient for time *t*, channel *c* begins at byte index `1024*t + 4*c + 1`.
