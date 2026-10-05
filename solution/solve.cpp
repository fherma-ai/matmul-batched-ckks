// P products of square matrices over CKKS, a pair at a time.
//
// The circuit for one pair is Aikata's winning answer to the FHERMA Matrix
// Multiplication challenge, as published in fairmath/polycircuit (Apache-2.0)
// and measured here unchanged: row-wise encoding, one ciphertext per matrix,
// depth 2, O(d + d·log d) rotations. What this file adds is the batch — the
// circuit run P times over P pairs of ciphertexts.
//
// That is the honest shape of a CKKS answer to this specification. A scheme
// whose ciphertext holds a batch does the whole point in one operation; this
// one holds a matrix, so it does the point P times. Nothing here tries to
// hide that, and the number it produces is what a batch costs an answer built
// this way.
//
// The loop is serial on purpose. The circuit inside is already parallel over
// its d/2 independent products, and OpenMP does not nest by default: an outer
// parallel loop would turn the inner ones into serial code and measure a
// different program.
#include <cmath>
#include <vector>

#include "solve.h"

namespace {

// The two masks that split A column-wise and B row-wise. They follow from the
// point and the context alone, which is what solve_init is for — and they are
// the same for every pair in the batch, so they are built once.
struct Masks {
    Plaintext columns;
    Plaintext rows;
    int d;
};

// One pair, by the challenge circuit.
Ciphertext<DCRTPoly> product(CryptoContext<DCRTPoly> cc, const Masks& masks,
                             const Ciphertext<DCRTPoly>& ca,
                             const Ciphertext<DCRTPoly>& cb) {
    const int d = masks.d;

    auto A = cc->EvalAdd(ca, cc->EvalRotate(ca, -d * d + 1));
    auto B = cc->EvalAdd(cb, cc->EvalRotate(cb, -d * d + d));
    const int d_log = static_cast<int>(std::log2(d));
    const int half_log = static_cast<int>(std::log2(d / 2));

    std::vector<Ciphertext<DCRTPoly>> out(d / 2);

    #pragma omp parallel for shared(A, B, masks)
    for (int t = 0; t < d / 2; t++) {
        std::vector<Ciphertext<DCRTPoly>> ab1(2), ab2(2);
        if (t != 0) {
            ab1[0] = cc->EvalRotate(A, 2 * t);
            ab1[1] = cc->EvalRotate(B, 2 * t * d);
        } else {
            ab1[0] = A;
            ab1[1] = B;
        }
        for (int j = 0; j < 2; j++) {
            ab1[j] = cc->EvalMult(ab1[j], j == 0 ? masks.columns : masks.rows);
            for (int k = 0; k < d_log; k++) {
                const int l = -1 * static_cast<int>(std::pow(2, k));
                ab2[j] = cc->EvalRotate(ab1[j], l * static_cast<int>(std::pow(d, j)));
                ab1[j] = cc->EvalAdd(ab1[j], ab2[j]);
            }
        }
        out[t] = cc->EvalMult(ab1[0], ab1[1]);
    }

    for (int i = 1; i <= half_log; i++) {
        const int stride = static_cast<int>(d / std::pow(2, i + 1));
        #pragma omp parallel for
        for (int t = 0; t < stride; t++) {
            cc->EvalAddInPlace(out[t], out[t + stride]);
        }
    }
    return cc->EvalAdd(out[0], cc->EvalRotate(out[0], d * d));
}

}  // namespace

void* solve_init(const fherma::Point& p, CryptoContext<DCRTPoly> cc) {
    const int d = static_cast<int>(p.N);

    std::vector<std::vector<double>> mask(2, std::vector<double>(2 * d * d, 0));
    for (int k = 0; k < 4; k++) {
        for (int i = 0; i < d; i++) {
            const int t = (k % 2) * i + (1 - (k % 2)) * (i * d);
            mask[k % 2][t + (k / 2) * d * d] = 1;
        }
    }

    auto* masks = new Masks;
    masks->columns = cc->MakeCKKSPackedPlaintext(mask[0]);
    masks->rows = cc->MakeCKKSPackedPlaintext(mask[1]);
    masks->d = d;
    return masks;
}

std::vector<Plaintext> solve_encoding(CryptoContext<DCRTPoly> cc,
                                      const fherma::Inputs& inp) {
    // One plaintext per matrix, row-major: slot i·N+j holds element [i][j].
    // The upper half of each packing stays free for the circuit's copies.
    // Order: every matrix of a, then every matrix of b, so solve_run reads
    // pair i as cts[i] and cts[P + i].
    const std::size_t pairs = inp.a.shape.empty() ? 0 : static_cast<std::size_t>(inp.a.shape[0]);
    const std::size_t square = pairs ? inp.a.data.size() / pairs : 0;

    std::vector<Plaintext> out;
    out.reserve(2 * pairs);
    for (const auto* tensor : { &inp.a, &inp.b }) {
        for (std::size_t i = 0; i < pairs; i++) {
            const auto begin = tensor->data.begin() + static_cast<std::ptrdiff_t>(i * square);
            std::vector<double> one(begin, begin + static_cast<std::ptrdiff_t>(square));
            out.push_back(cc->MakeCKKSPackedPlaintext(one));
        }
    }
    return out;
}

std::vector<Ciphertext<DCRTPoly>> solve_run(
    void* state,
    CryptoContext<DCRTPoly> cc,
    const std::vector<Ciphertext<DCRTPoly>>& cts) {
    const auto* masks = static_cast<const Masks*>(state);
    const std::size_t pairs = cts.size() / 2;

    std::vector<Ciphertext<DCRTPoly>> out;
    out.reserve(pairs);
    for (std::size_t i = 0; i < pairs; i++) {
        out.push_back(product(cc, *masks, cts[i], cts[pairs + i]));
    }
    return out;
}

fherma::Outputs solve_decoding(const fherma::Point& p,
                               CryptoContext<DCRTPoly> cc,
                               const std::vector<Plaintext>& pts) {
    const auto n = static_cast<int64_t>(p.N);
    const auto square = static_cast<std::size_t>(n * n);

    fherma::Outputs out;
    out.c.shape = { static_cast<int64_t>(p.P), n, n };
    out.c.data.reserve(pts.size() * square);
    for (const auto& pt : pts) {
        auto values = pt->GetRealPackedValue();
        out.c.data.insert(out.c.data.end(), values.begin(),
                          values.begin() + static_cast<std::ptrdiff_t>(square));
    }
    return out;
}

void solve_free(void* state) {
    delete static_cast<Masks*>(state);
}
