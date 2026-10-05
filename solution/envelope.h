// GENERATED for matmul/secret-matrix-batches@1.0.0. Do not edit — `--update` rewrites it.
//
// The cryptographic envelope: context, keys, encryption, decryption. The only
// place a secret key exists. The measured call never enters this file.
//
// Every parameter comes from config.jsonc; the scheme named there decides
// which of them are read, so the config can switch schemes without
// re-scaffolding.
#pragma once

#include <fstream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include "openfhe.h"

namespace fherma_envelope {

struct Config {
    std::string scheme = "ckks";
    std::string security = "HEStd_128_classic";
    uint32_t ring_dimension = 16384;
    uint32_t mult_depth = 2;
    uint32_t batch_size = 8192;
    std::vector<int32_t> rotation_key_indexes;
    bool generate_sum_keys = false;
    // ckks
    uint32_t scale_mod_size = 50;
    uint32_t first_mod_size = 60;
    std::string scaling_technique = "FLEXIBLEAUTO";
    bool enable_bootstrapping = false;
    std::vector<uint32_t> level_budget = {4, 4};
    uint32_t levels_available_after_bootstrap = 10;
    // bgv / bfv
    uint64_t plaintext_modulus = 65537;
    uint32_t max_relin_sk_deg = 2;

    static Config load(const std::string& path);   // tiny JSONC reader in main
};

class Envelope {
  public:
    explicit Envelope(const Config& cfg) {
        const bool bootstrap = cfg.scheme == "ckks" && cfg.enable_bootstrapping;

        if (cfg.scheme == "ckks") {
            lbcrypto::CCParams<lbcrypto::CryptoContextCKKSRNS> parameters;
            parameters.SetScalingModSize(cfg.scale_mod_size);
            parameters.SetFirstModSize(cfg.first_mod_size);
            parameters.SetScalingTechnique(scaling(cfg.scaling_technique));
            uint32_t depth = cfg.mult_depth;
            if (bootstrap)
                depth = cfg.levels_available_after_bootstrap +
                        lbcrypto::FHECKKSRNS::GetBootstrapDepth(
                            cfg.level_budget, lbcrypto::UNIFORM_TERNARY);
            finish(parameters, cfg, depth);
        } else if (cfg.scheme == "bgv") {
            lbcrypto::CCParams<lbcrypto::CryptoContextBGVRNS> parameters;
            parameters.SetPlaintextModulus(cfg.plaintext_modulus);
            parameters.SetMaxRelinSkDeg(cfg.max_relin_sk_deg);
            finish(parameters, cfg, cfg.mult_depth);
        } else if (cfg.scheme == "bfv") {
            lbcrypto::CCParams<lbcrypto::CryptoContextBFVRNS> parameters;
            parameters.SetPlaintextModulus(cfg.plaintext_modulus);
            parameters.SetMaxRelinSkDeg(cfg.max_relin_sk_deg);
            finish(parameters, cfg, cfg.mult_depth);
        } else {
            throw std::runtime_error("config.jsonc: unknown scheme " + cfg.scheme);
        }

        cc_->Enable(lbcrypto::PKE);
        cc_->Enable(lbcrypto::KEYSWITCH);
        cc_->Enable(lbcrypto::LEVELEDSHE);
        cc_->Enable(lbcrypto::ADVANCEDSHE);
        if (bootstrap) cc_->Enable(lbcrypto::FHE);

        keys_ = cc_->KeyGen();
        cc_->EvalMultKeyGen(keys_.secretKey);
        if (!cfg.rotation_key_indexes.empty())
            cc_->EvalRotateKeyGen(keys_.secretKey, cfg.rotation_key_indexes);
        if (cfg.generate_sum_keys) cc_->EvalSumKeyGen(keys_.secretKey);
        if (bootstrap) {
            cc_->EvalBootstrapSetup(cfg.level_budget, {0, 0}, cfg.batch_size);
            cc_->EvalBootstrapKeyGen(keys_.secretKey, cfg.batch_size);
        }
    }

    lbcrypto::CryptoContext<lbcrypto::DCRTPoly> cc() const { return cc_; }

    std::vector<lbcrypto::Ciphertext<lbcrypto::DCRTPoly>>
    encrypt(const std::vector<lbcrypto::Plaintext>& packings) {
        std::vector<lbcrypto::Ciphertext<lbcrypto::DCRTPoly>> out;
        out.reserve(packings.size());
        for (const auto& p : packings) out.push_back(cc_->Encrypt(keys_.publicKey, p));
        return out;
    }

    std::vector<lbcrypto::Plaintext>
    decrypt(const std::vector<lbcrypto::Ciphertext<lbcrypto::DCRTPoly>>& cts) {
        std::vector<lbcrypto::Plaintext> out;
        out.reserve(cts.size());
        for (const auto& ct : cts) {
            lbcrypto::Plaintext p;
            cc_->Decrypt(keys_.secretKey, ct, &p);
            out.push_back(p);
        }
        return out;
    }

  private:
    // The parameters every scheme shares, and the context they produce.
    template <typename P>
    void finish(P& parameters, const Config& cfg, uint32_t depth) {
        parameters.SetRingDim(cfg.ring_dimension);
        parameters.SetBatchSize(cfg.batch_size);
        parameters.SetMultiplicativeDepth(depth);
        // The declared level, enforced by the library itself.
        static const std::map<std::string, lbcrypto::SecurityLevel> LEVELS = {
            {"HEStd_128_classic", lbcrypto::HEStd_128_classic},
            {"HEStd_192_classic", lbcrypto::HEStd_192_classic},
            {"HEStd_256_classic", lbcrypto::HEStd_256_classic},
        };
        parameters.SetSecurityLevel(LEVELS.at(cfg.security));
        cc_ = GenCryptoContext(parameters);
    }

    static lbcrypto::ScalingTechnique scaling(const std::string& name) {
        static const std::map<std::string, lbcrypto::ScalingTechnique> T = {
            {"FIXEDMANUAL", lbcrypto::FIXEDMANUAL},
            {"FIXEDAUTO", lbcrypto::FIXEDAUTO},
            {"FLEXIBLEAUTO", lbcrypto::FLEXIBLEAUTO},
            {"FLEXIBLEAUTOEXT", lbcrypto::FLEXIBLEAUTOEXT},
        };
        return T.at(name);
    }

    lbcrypto::CryptoContext<lbcrypto::DCRTPoly> cc_;
    lbcrypto::KeyPair<lbcrypto::DCRTPoly> keys_;   // never handed out
};

}  // namespace fherma_envelope
