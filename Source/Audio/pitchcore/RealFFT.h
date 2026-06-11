// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Utawave
//
// PitchCore — 自前の実数 FFT (外部ライブラリ非依存・C++17 のみ)
//
// radix-2 Cooley-Tukey (反復・ビットリバース) を土台に、実信号 N 点を
// 半サイズ N/2 の複素 FFT 1 回へパッキングする標準手法で実数 FFT を実装する。
//   forward: 実信号 time[N]   → スペクトル spec[N+2] (bins 0..N/2 を re,im 交互)
//   inverse: spec[N+2]        → 実信号 time[N] (1/N スケーリング込み)
// forward → inverse は恒等 (浮動小数点誤差を除く)。
// インスタンスは内部ワークバッファを持つため、スレッド毎に別インスタンスを使うこと。

#pragma once
#include <cmath>
#include <complex>
#include <vector>

namespace pitchcore
{

class RealFFT
{
public:
    // size: 8 以上の 2 の冪
    explicit RealFFT(int size) : n(size), m(size / 2)
    {
        int bits = 0;
        while ((1 << bits) < m) ++bits;

        bitrev.resize((size_t) m);
        for (int i = 0; i < m; ++i)
        {
            int r = 0;
            for (int b = 0; b < bits; ++b)
                if (i & (1 << b)) r |= 1 << (bits - 1 - b);
            bitrev[(size_t) i] = r;
        }

        twiddle.resize((size_t)(m / 2));
        for (int j = 0; j < m / 2; ++j)
            twiddle[(size_t) j] = std::complex<float>(
                (float) std::cos(-2.0 * kPi * j / m),
                (float) std::sin(-2.0 * kPi * j / m));

        packTw.resize((size_t)(m + 1));
        for (int k = 0; k <= m; ++k)
            packTw[(size_t) k] = std::complex<float>(
                (float) std::cos(-2.0 * kPi * k / n),
                (float) std::sin(-2.0 * kPi * k / n));

        work.resize((size_t) m);
        lin.resize((size_t) m);
    }

    int getSize() const { return n; }

    void forward(const float* time, float* spec)
    {
        // z[k] = x[2k] + i*x[2k+1] をビットリバース配置で work へ
        for (int k = 0; k < m; ++k)
            work[(size_t) bitrev[(size_t) k]] =
                std::complex<float>(time[2 * k], time[2 * k + 1]);
        fftInPlace(false);

        // 偶数列/奇数列のスペクトルへ分解して結合: X[k] = Fe[k] + e^{-2πik/n}·Fo[k]
        for (int k = 0; k <= m; ++k)
        {
            const std::complex<float> zk  = work[(size_t)(k % m)];
            const std::complex<float> zmk = std::conj(work[(size_t)((m - k) % m)]);
            const std::complex<float> fe  = 0.5f * (zk + zmk);
            const std::complex<float> fo  = std::complex<float>(0.0f, -0.5f) * (zk - zmk);
            const std::complex<float> x   = fe + packTw[(size_t) k] * fo;
            spec[2 * k]     = x.real();
            spec[2 * k + 1] = x.imag();
        }
    }

    void inverse(const float* spec, float* time)
    {
        // 結合の逆: Fe[k] = (X[k]+conj(X[m-k]))/2, Fo[k] = conj(tw[k])·(X[k]-conj(X[m-k]))/2,
        // Z[k] = Fe[k] + i·Fo[k]
        for (int k = 0; k < m; ++k)
        {
            const std::complex<float> xk  { spec[2 * k],        spec[2 * k + 1] };
            const std::complex<float> xmk { spec[2 * (m - k)], -spec[2 * (m - k) + 1] };
            const std::complex<float> fe = 0.5f * (xk + xmk);
            const std::complex<float> fo = std::conj(packTw[(size_t) k])
                                         * (0.5f * (xk - xmk));
            lin[(size_t) k] = fe + std::complex<float>(0.0f, 1.0f) * fo;
        }
        for (int k = 0; k < m; ++k)
            work[(size_t) bitrev[(size_t) k]] = lin[(size_t) k];
        fftInPlace(true);

        const float scale = 1.0f / (float) m;
        for (int k = 0; k < m; ++k)
        {
            time[2 * k]     = work[(size_t) k].real() * scale;
            time[2 * k + 1] = work[(size_t) k].imag() * scale;
        }
    }

private:
    static constexpr double kPi = 3.14159265358979323846;

    void fftInPlace(bool inv)
    {
        for (int len = 2; len <= m; len <<= 1)
        {
            const int half   = len >> 1;
            const int stride = m / len;
            for (int base = 0; base < m; base += len)
            {
                for (int j = 0; j < half; ++j)
                {
                    std::complex<float> w = twiddle[(size_t)(j * stride)];
                    if (inv) w = std::conj(w);
                    auto& a = work[(size_t)(base + j)];
                    auto& b = work[(size_t)(base + j + half)];
                    const std::complex<float> t = w * b;
                    b = a - t;
                    a += t;
                }
            }
        }
    }

    int n, m;
    std::vector<int> bitrev;
    std::vector<std::complex<float>> twiddle;   // 複素 FFT (サイズ m) 用
    std::vector<std::complex<float>> packTw;    // 実信号パッキング用 e^{-2πik/n}
    std::vector<std::complex<float>> work, lin; // ワークバッファ (非スレッドセーフ)
};

} // namespace pitchcore
