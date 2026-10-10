// Python-compatible random.Random: same MT19937 seeding (init_by_array), random(), uniform(), randint() and choice(),
// so every seeded layout (forest, graveyard, herd, garden...) comes out exactly as it does in the Python ani-3d.
#pragma once
#include <cstdint>
#include <cstdlib>
#include <vector>

class PyRandom {
public:
    explicit PyRandom(long long seed = 0) { seed_int(seed); }

    void seed_int(long long seed) {
        unsigned long long n = (unsigned long long)(seed < 0 ? -seed : seed);
        std::vector<uint32_t> key;
        if (n == 0) key.push_back(0);
        while (n) { key.push_back((uint32_t)(n & 0xffffffffu)); n >>= 32; }
        init_by_array(key);
    }

    uint32_t genrand() {
        static const uint32_t mag01[2] = {0x0u, 0x9908b0dfu};
        uint32_t y;
        if (mti >= N) {
            int kk;
            for (kk = 0; kk < N - M; kk++) {
                y = (mt[kk] & 0x80000000u) | (mt[kk + 1] & 0x7fffffffu);
                mt[kk] = mt[kk + M] ^ (y >> 1) ^ mag01[y & 1u];
            }
            for (; kk < N - 1; kk++) {
                y = (mt[kk] & 0x80000000u) | (mt[kk + 1] & 0x7fffffffu);
                mt[kk] = mt[kk + (M - N)] ^ (y >> 1) ^ mag01[y & 1u];
            }
            y = (mt[N - 1] & 0x80000000u) | (mt[0] & 0x7fffffffu);
            mt[N - 1] = mt[M - 1] ^ (y >> 1) ^ mag01[y & 1u];
            mti = 0;
        }
        y = mt[mti++];
        y ^= (y >> 11);
        y ^= (y << 7) & 0x9d2c5680u;
        y ^= (y << 15) & 0xefc60000u;
        y ^= (y >> 18);
        return y;
    }

    double random() {
        uint32_t a = genrand() >> 5, b = genrand() >> 6;
        return (a * 67108864.0 + b) * (1.0 / 9007199254740992.0);
    }
    double uniform(double a, double b) { return a + (b - a) * random(); }

    uint32_t getrandbits(int k) { return k <= 0 ? 0 : genrand() >> (32 - k); }
    long long randbelow(long long n) {
        if (n <= 0) return 0;
        int k = 0;
        for (unsigned long long v = (unsigned long long)n; v; v >>= 1) k++;
        long long r = getrandbits(k);
        while (r >= n) r = getrandbits(k);
        return r;
    }
    long long randint(long long a, long long b) { return a + randbelow(b - a + 1); }
    template <class T> const T& choice(const std::vector<T>& v) { return v[(size_t)randbelow((long long)v.size())]; }
    template <class T, size_t K> const T& choice(const T (&v)[K]) { return v[(size_t)randbelow((long long)K)]; }
    size_t index(size_t n) { return (size_t)randbelow((long long)n); }

private:
    static constexpr int N = 624, M = 397;
    uint32_t mt[N];
    int mti = N + 1;

    void init_genrand(uint32_t s) {
        mt[0] = s;
        for (mti = 1; mti < N; mti++) mt[mti] = (1812433253u * (mt[mti - 1] ^ (mt[mti - 1] >> 30)) + (uint32_t)mti);
    }
    void init_by_array(const std::vector<uint32_t>& key) {
        init_genrand(19650218u);
        int i = 1, j = 0;
        int len = (int)key.size();
        int k = N > len ? N : len;
        for (; k; k--) {
            mt[i] = (mt[i] ^ ((mt[i - 1] ^ (mt[i - 1] >> 30)) * 1664525u)) + key[j] + (uint32_t)j;
            i++; j++;
            if (i >= N) { mt[0] = mt[N - 1]; i = 1; }
            if (j >= len) j = 0;
        }
        for (k = N - 1; k; k--) {
            mt[i] = (mt[i] ^ ((mt[i - 1] ^ (mt[i - 1] >> 30)) * 1566083941u)) - (uint32_t)i;
            i++;
            if (i >= N) { mt[0] = mt[N - 1]; i = 1; }
        }
        mt[0] = 0x80000000u;
        mti = N;
    }
};
