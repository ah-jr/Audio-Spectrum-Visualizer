#pragma once

#include <complex>
#include <vector>
#include <cmath>

namespace fft {

using Complex = std::complex<double>;
using ComplexVector = std::vector<Complex>;

/**
 * Cooley-Tukey FFT Algorithm (Radix-2 DIT)
 * Computes the Discrete Fourier Transform of the input signal
 * 
 * @param signal Input signal (must be power of 2 in length)
 * @return Complex frequency domain representation
 */
ComplexVector transform(const std::vector<double>& signal);

/**
 * In-place FFT computation
 * @param data Complex vector to transform in place
 */
void transformInPlace(ComplexVector& data);

/**
 * Inverse FFT
 * @param spectrum Frequency domain data
 * @return Time domain signal
 */
ComplexVector inverse(const ComplexVector& spectrum);

/**
 * Compute magnitude spectrum from complex FFT output
 * @param spectrum Complex FFT output
 * @return Magnitude values
 */
std::vector<double> magnitude(const ComplexVector& spectrum);

/**
 * Compute power spectrum (magnitude squared) in dB
 * @param spectrum Complex FFT output
 * @param minDb Minimum dB value (for floor)
 * @return Power values in dB
 */
std::vector<double> powerDb(const ComplexVector& spectrum, double minDb = -100.0);

/**
 * Apply Hann window to signal
 * @param signal Input signal
 * @return Windowed signal
 */
std::vector<double> applyHannWindow(const std::vector<double>& signal);

/**
 * Apply Hamming window to signal
 * @param signal Input signal
 * @return Windowed signal
 */
std::vector<double> applyHammingWindow(const std::vector<double>& signal);

/**
 * Apply Blackman window to signal
 * @param signal Input signal
 * @return Windowed signal
 */
std::vector<double> applyBlackmanWindow(const std::vector<double>& signal);

/**
 * Get the next power of 2 >= n
 * @param n Input value
 * @return Next power of 2
 */
size_t nextPowerOf2(size_t n);

/**
 * Check if n is a power of 2
 * @param n Input value
 * @return True if power of 2
 */
bool isPowerOf2(size_t n);

} // namespace fft


