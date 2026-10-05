# Batched matrix multiplication over CKKS — OpenFHE

> Implements [`matrix-multiplication` / `secret-matrix-batches@1.0.0`](https://www.fherma.io/kernels/matrix-multiplication/specifications/secret-matrix-batches)
> on the FHERMA kernel catalogue.
>
> The circuit for one pair is [Aikata](https://www.iaik.tugraz.at/person/aikata-aikata/)'s
> winning entry to the [FHERMA Matrix Multiplication challenge](https://fherma.io/challenges/652bf669485c878710fd020b),
> published as the MatrixMultiplication component of
> [polycircuit](https://github.com/fairmath/polycircuit) (Apache-2.0) and used
> here unchanged. What this repository adds is the batch.

P pairs of N×N matrices, multiplied pairwise — `c[i] = a[i] · b[i]`.

```text
kernel matmul<P: u32, N: u32>(
    %a: secret<tensor<P x N x N x f64>>,
    %b: secret<tensor<P x N x N x f64>>,
) -> %c: secret<tensor<P x N x N x f64>>
```

## How it spends the batch

A CKKS ciphertext here holds one matrix, so P pairs are P runs of the circuit:

```cpp
for (std::size_t i = 0; i < pairs; i++) {
    out.push_back(product(cc, *masks, cts[i], cts[pairs + i]));
}
```

That is the honest shape of this answer, and the point of measuring it. A
scheme whose ciphertext holds a batch does the whole point in one operation;
this one holds a matrix, so it does the point P times. The number it produces
is what a batch costs a solution built this way — measured rather than
estimated by multiplying one pair by P.

The loop is serial on purpose. The circuit inside is already parallel over its
d/2 independent products, and OpenMP does not nest by default: an outer
parallel loop would turn the inner ones into serial code and measure a
different program.

## Parameters

Chosen to match the configuration DESILO published in
[CKKS vs. GL](https://desilo.ai/insights/blog/ckks-vs-gl-benchmark), so the two
sides of that comparison can be measured on one machine instead of compared
across two write-ups:

| Setting | Here | Why |
|---|---|---|
| ring dimension | 2¹⁵ | theirs |
| security | `HEStd_128_classic` | theirs, and OpenFHE refuses a parameter set that does not reach it |
| multiplicative depth | 2 | the circuit's, and their minimum-level setting |
| scaling modulus | 40 bits | theirs |
| batch size | 8192 | the circuit uses 2·d² slots for d = 64 |

## Layout

```
solution/
  solve.cpp      the circuit, and the loop over the batch
  solve.h        generated — the four functions, declared
  config.jsonc   the context: ring, depth, moduli, rotation keys
  fherma.toml    how it is built and started
  envelope.h     generated — context, keys, encryption. Holds the secret key
  main.cpp       generated — the measured loop
  fherma.h       generated — the types, from the signature
```

Only `solve.cpp` and `config.jsonc` are written by hand. The rest is emitted by
`fherma-lang` from the specification's signature and replaced at every
measurement, so a solution cannot drift from the contract it claims to meet.

## Running it yourself

In an image with OpenFHE 1.6.0:

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build
./build/solution <point directory>
```

A point directory is what the specification's testing bundle writes with
`main.py make`. The result is judged by the same bundle with `main.py verify` —
element-wise, to an absolute tolerance of 1e-2 against the cleartext product.
