#include "fft.hpp"
#include <algorithm>
#include <stdexcept>

// Safe max macro
#define SAFE_MAX(a, b) (((a) > (b)) ? (a) : (b))

namespace fft {

constexpr double PI = 3.14159265358979323846;

bool isPowerOf2(size_t n) {
    return n > 0 && (n & (n - 1)) == 0;
}

size_t nextPowerOf2(size_t n) {
    if (n == 0) return 1;
    n--;
    n |= n >> 1;
    n |= n >> 2;
    n |= n >> 4;
    n |= n >> 8;
    n |= n >> 16;
    n |= n >> 32;
    return n + 1;
}

// Bit-reversal permutation
static void bitReversePermute(ComplexVector& data) {
    size_t n = data.size();
    size_t j = 0;
    
    for (size_t i = 0; i < n - 1; ++i) {
        if (i < j) {
            std::swap(data[i], data[j]);
        }
        
        size_t k = n >> 1;
        while (k <= j) {
            j -= k;
            k >>= 1;
        }
        j += k;
    }
}

void transformInPlace(ComplexVector& data) {
    size_t n = data.size();
    
    if (!isPowerOf2(n)) {
        throw std::invalid_argument("FFT size must be a power of 2");
    }
    
    if (n <= 1) return;
    
    // Bit-reversal permutation
    bitReversePermute(data);
    
    // Cooley-Tukey iterative FFT
    for (size_t len = 2; len <= n; len <<= 1) {
        double angle = -2.0 * PI / static_cast<double>(len);
        Complex wlen(std::cos(angle), std::sin(angle));
        
        for (size_t i = 0; i < n; i += len) {
            Complex w(1.0, 0.0);
            
            for (size_t j = 0; j < len / 2; ++j) {
                Complex u = data[i + j];
                Complex t = w * data[i + j + len / 2];
                
                data[i + j] = u + t;
                data[i + j + len / 2] = u - t;
                
                w *= wlen;
            }
        }
    }
}

ComplexVector transform(const std::vector<double>& signal) {
    size_t n = nextPowerOf2(signal.size());
    ComplexVector data(n, Complex(0.0, 0.0));
    
    for (size_t i = 0; i < signal.size(); ++i) {
        data[i] = Complex(signal[i], 0.0);
    }
    
    transformInPlace(data);
    return data;
}

ComplexVector inverse(const ComplexVector& spectrum) {
    ComplexVector data = spectrum;
    size_t n = data.size();
    
    // Conjugate the complex numbers
    for (auto& c : data) {
        c = std::conj(c);
    }
    
    // Forward FFT
    transformInPlace(data);
    
    // Conjugate and scale
    for (auto& c : data) {
        c = std::conj(c) / static_cast<double>(n);
    }
    
    return data;
}

std::vector<double> magnitude(const ComplexVector& spectrum) {
    std::vector<double> result(spectrum.size());
    
    for (size_t i = 0; i < spectrum.size(); ++i) {
        result[i] = std::abs(spectrum[i]);
    }
    
    return result;
}

std::vector<double> powerDb(const ComplexVector& spectrum, double minDb) {
    std::vector<double> result(spectrum.size());
    double refPower = 1.0;
    
    for (size_t i = 0; i < spectrum.size(); ++i) {
        double mag = std::abs(spectrum[i]);
        double power = mag * mag;
        
        if (power > 0) {
            result[i] = 10.0 * std::log10(power / refPower);
            result[i] = SAFE_MAX(result[i], minDb);
        } else {
            result[i] = minDb;
        }
    }
    
    return result;
}

std::vector<double> applyHannWindow(const std::vector<double>& signal) {
    std::vector<double> result(signal.size());
    size_t n = signal.size();
    
    for (size_t i = 0; i < n; ++i) {
        double multiplier = 0.5 * (1.0 - std::cos(2.0 * PI * i / (n - 1)));
        result[i] = signal[i] * multiplier;
    }
    
    return result;
}

std::vector<double> applyHammingWindow(const std::vector<double>& signal) {
    std::vector<double> result(signal.size());
    size_t n = signal.size();
    
    for (size_t i = 0; i < n; ++i) {
        double multiplier = 0.54 - 0.46 * std::cos(2.0 * PI * i / (n - 1));
        result[i] = signal[i] * multiplier;
    }
    
    return result;
}

std::vector<double> applyBlackmanWindow(const std::vector<double>& signal) {
    std::vector<double> result(signal.size());
    size_t n = signal.size();
    
    for (size_t i = 0; i < n; ++i) {
        double multiplier = 0.42 - 0.5 * std::cos(2.0 * PI * i / (n - 1)) 
                          + 0.08 * std::cos(4.0 * PI * i / (n - 1));
        result[i] = signal[i] * multiplier;
    }
    
    return result;
}

} // namespace fft

