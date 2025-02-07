#include "bonsai/encoder.h"
#include "bonsai/util.h"
#include "kseq_declare.h"
//#include "hll/flat_hash_map/flat_hash_map.hpp"
#include <getopt.h>
#ifdef _OPENMP
#include <omp.h>
#endif
#include "sketch/setsketch.h"

using namespace bns;

#ifndef CSETFT
#define CSETFT double
#endif

template<typename Sketch>
void update_sketch(Encoder<> &enc, RollingHasher<uint64_t> &rolling_hasher, Sketch &sketch, const std::string &path, const int htype, kseq_t *kseq=static_cast<kseq_t*>(nullptr)) {
    auto update_fn = [&sketch](uint64_t x) {
        sketch.update(x);
    };
    if(htype == 0) {
        enc.for_each(update_fn, path.data(), kseq);
    } else if(htype == 1) {
        rolling_hasher.for_each_hash(update_fn, path.data(), kseq);
    } else if(htype == 2) {
        enc.for_each_hash(update_fn, path.data(), kseq);
    } else {
        std::fprintf(stderr, "Error: this should never happen. htype should be [0, 1, 2]\n");
        std::exit(EXIT_FAILURE);
    }
}

struct PlusEq {
    template<typename T>
    T &operator()(T &lhs, const T &rhs) const {return lhs += rhs;}
};

using namespace sketch;
using namespace sketch::setsketch;

using SSType = CSetSketch<CSETFT>;

template<typename T, typename Func=PlusEq>
void par_reduce(T *x, size_t n, const Func &func=Func()) {
    const unsigned int ln = static_cast<int>(std::ceil(std::log2(n)));
    for(size_t i = 0; i < ln; ++i) {
        const size_t step_size = 1 << i;
        const size_t sweep_size = (i + 1) << 1;
        const size_t nsweeps = (n + (sweep_size - 1)) / sweep_size;
        OMP_PFOR
        for(size_t j = 0; j < nsweeps; ++j) {
            const auto lh = j * sweep_size, rh = lh + step_size;
            if(rh < n)
                func(x[lh], x[rh]);
        }
    }
}


void usage() {
    std::fprintf(stderr, "Usage: setsketcher <opts> (input paths) \n"
                        " Opts: [-k [31] -o [default.sketch] -C [true] -N [use_nthash] -c [use_cyclic_hash] -p [1]] <paths>\n"
                        "-k: Set k-mer length\n"
                        "-C: Do not canonicalize\n"
                        "-p: Set number of threads\n"
                        "-s: Save kmers. This yields additional files consisting of sampled k-mers as well. (This can be used to build estimators for GC bias, Shannon entropy, and seeding)\n"
                        "-S: Save kmer counts [Implies -s]. This yields additional files describing multiplicities of k-mers.\n"
                        "-z: Set sketch size (default: 4096)\n"
                        "-F: Load paths from <file> (in addition to positional arguments)\n"
                        "-P: Parse protein k-mers instead of DNA k-mers [this implies cyclic, avoiding direct encoding]\n"
                        "-I: Set initial buffer size for sequence parsing to [size_t] (4194304 = 4MiB)\n"
                        "-Z: Do not save sketches for individual files. Default behavior saves sketches for each file and also emits the union sketch.\n"
                        "-B: Store per-sample setsketches and k-mer samples in current directory instead of the file containing the sequence files\n"
        );
}

int main(int argc, char **argv) {
    std::string ofile = "default.sketch", fpaths;
    std::string kmerparsetype = "bns";
    bool canon = true, enable_protein = false,
        save_kmers = 0, save_kmer_counts = 0,
        save_sketches = 1;
    int k = 31, nthreads = 1;
    size_t initsize = 1ull << 20, sketchsize = 4096;
    std::FILE *logfp = stderr;
    bool basename = false;
    CSETFT startmax = std::numeric_limits<CSETFT>::max();
    for(int c;(c = getopt(argc, argv, "BL:Y:I:k:F:o:p:z:ZPsScCNh?")) >= 0;) {
        switch(c) {
            case 'B': basename = true; break;
            case 'Z': save_sketches = 0; break;
            case 'k': k = std::atoi(optarg); break;
            case 'o': ofile = optarg; break;
            case 'L': if(!(logfp = std::fopen(optarg, "w"))) throw std::runtime_error("Failed to open logfile");
                      break;
            case 'h': usage(); return EXIT_FAILURE;
            case 'N': kmerparsetype = "nthash"; break;
            case 'C': canon = false; break;
            case 'c': kmerparsetype = "cyclic"; break;
            case 'z': sketchsize = std::strtoull(optarg, nullptr, 10); break;
            case 'S': save_kmers = true; break;
            case 's': save_kmer_counts = save_kmers = true; break;
            case 'F': fpaths = optarg; break;
            case 'p': nthreads = std::atoi(optarg); break;
            case 'P': enable_protein = true; kmerparsetype = "cyclic"; break;
            case 'I': initsize = std::strtoull(optarg, nullptr, 10); break;
            case 'Y': startmax = std::atof(optarg); break;
            //case 'S': spacestr = optarg; break;
        }
    }
    std::vector<std::string> infiles(argv + optind, argv + argc);
    if(fpaths.size()) {
        std::ifstream ifs(fpaths);
        for(std::string line;std::getline(ifs, line);) {
            infiles.emplace_back(line);
        }
    }
    if(infiles.empty()) {
        std::fprintf(stderr, "Reading from stdin. If this is unintended, see usage.\n");
        infiles.emplace_back("/dev/stdin");
    }
    if(enable_protein) {
        if(kmerparsetype != "cyclic") {
            std::fprintf(logfp, "Warning: enable_protein implies cyclic hashing.\n");
            kmerparsetype = "cyclic";
        }
    } else if(k > 32 && kmerparsetype == "bns") {
        std::fprintf(logfp, "Warning: k > 32 implies nthash-based rolling hashing.\n");
        kmerparsetype = "nthash";
    }
    if((save_kmers || save_kmer_counts) && (ofile == "/dev/stdout" || ofile == "-")) {
        std::fprintf(logfp, "Error: -o required if save_kmers or save_kmer_counts is set. (This yields multiple files using -o as a prefix.)");
        std::exit(EXIT_FAILURE);
    }
    nthreads = std::max(nthreads, 1);
#ifdef _OPENMP
    omp_set_num_threads(nthreads);
#endif
    std::fprintf(logfp, "Sketching %s %u-mers, %s\n", canon ? "canonical": "stranded", k, &kmerparsetype[0]);
    RollingHasher<uint64_t> *rencoders = static_cast<RollingHasher<uint64_t> *>(std::malloc(sizeof(RollingHasher<uint64_t>) * nthreads));
    Encoder<> *encoders = static_cast<Encoder<> *>(std::malloc(sizeof(Encoder<>) * nthreads));
    kseq_t *kseqs = static_cast<kseq_t *>(std::calloc(nthreads, sizeof(kseq_t)));
    SSType *sketches = static_cast<SSType *>(std::calloc(nthreads, sizeof(SSType)));
    SSType *usketches = nullptr;
    if(save_sketches) usketches = static_cast<SSType *>(std::calloc(nthreads, sizeof(SSType)));

    const RollingHashingType rht = enable_protein ? RollingHashingType::PROTEIN: RollingHashingType::DNA;
    OMP_PFOR
    for(int idx = 0; idx < nthreads; ++idx) {
        auto &back = kseqs[idx];
        back.seq.m = initsize;
        back.seq.s = static_cast<char *>(std::malloc(initsize));
        new (encoders + idx) Encoder<>(k, canon);
        new (rencoders + idx) RollingHasher<uint64_t>(k, canon, rht);
        new (sketches + idx) SSType(sketchsize, save_kmers, save_kmer_counts, startmax);
        if(usketches) {
            new(usketches + idx) SSType(sketchsize, save_kmers, save_kmer_counts, startmax);
        }
    }
    const int htype = kmerparsetype == "bns" ? 0: kmerparsetype == "cyclic"? 1: 2;
    CSETFT maxv = 0., minv = std::numeric_limits<CSETFT>::max();
    std::atomic<uint64_t> total_processed;
    total_processed.store(0);
#ifdef _OPENMP
    #pragma omp parallel for schedule(dynamic)
#endif
    for(size_t i = 0; i < infiles.size(); ++i) {
        const int tid = OMP_ELSE(omp_get_thread_num(), 0);
        auto &s = sketches[tid];
        if(s.total_updates()) s.clear();
        update_sketch(
                encoders[tid], rencoders[tid], s, // Parsing/Sketching prep
                infiles[i], htype, &kseqs[tid]    // Path/Sketch format/buffer
        );
        const size_t scard = s.cardinality();
        ++total_processed;
        std::fprintf(logfp, "%s\t%zu. Total updates %zu for %%%f unique (%zu/%zu)\n", infiles[i].data(), scard, s.total_updates(), 100. * scard / s.total_updates(), static_cast<size_t>(total_processed.load()), infiles.size());
        std::string filebasename = infiles[i];
        if(basename) {
            auto pos = filebasename.find_last_of('/');
            if(pos != std::string::npos) filebasename = std::string(&filebasename[pos + 1], &filebasename[filebasename.size()]);
        }
        maxv = std::max(maxv, CSETFT(s.max()));
        minv = std::min(minv, CSETFT(s.min()));
        if(save_sketches) {
            s.write(filebasename + "." + std::to_string(k) + "." + std::to_string(sketchsize) + ".ss");
            if(!usketches[tid].total_updates()) usketches[tid] = s;
            else usketches[tid] += s;
        }
        if(save_kmers) {
            const std::string path = filebasename + "." + std::to_string(k) + "." + std::to_string(sketchsize) + ".u64.kmers";
            std::ofstream ofs(path, std::ios::out | std::ios::binary);
            ofs.write((const char *)s.ids().data(), s.ids().size() * sizeof(s.ids()[0]));
            if(save_kmer_counts) {
                const std::string path = filebasename + "." + std::to_string(k) + "." + std::to_string(sketchsize) + ".u32.kmercounts";
                std::ofstream ofs(path, std::ios::out | std::ios::binary);
                ofs.write((const char *)s.idcounts().data(), s.idcounts().size() * sizeof(s.idcounts()[0]));
            }
        }
    }
    if(save_sketches) std::swap(sketches, usketches);
    auto t = std::chrono::high_resolution_clock::now();
    par_reduce(sketches, nthreads);
    std::fprintf(logfp, "union cardinality: %g\n", sketches->cardinality());
    if(!save_kmers) {
        sketches->write(ofile);
    } else {
        auto &f = *sketches;
        f.write(ofile + ".csketch");
        {
            {
                const std::string path = ofile + ".u64.kmers";
                std::ofstream ofs(path, std::ios::out | std::ios::binary);
                std::fprintf(logfp, "Writing k-mers to %s\n", path.data());
                ofs.write((const char *)f.ids().data(), f.ids().size() * sizeof(f.ids()[0]));
            }
            {
                std::ofstream ofshr(ofile + ".humanreadable.kmers", std::ios::out);
                const auto idp = f.ids().data();
                const auto idcp = f.idcounts().data();
                for(size_t i = 0; i < f.ids().size(); ++i) {
                    uint64_t id = idp[i];
                    if(htype == 0) ofshr << encoders->sp_.to_string(id);
                    else           ofshr << id;
                    if(f.idcounts().size()) ofshr << '\t' << idcp[i];
                    ofshr << '\n';
                }
            }
            {
                std::ofstream ofs(ofile + ".u32.kmercounts", std::ios::out | std::ios::binary);
                ofs.write((const char *)f.idcounts().data(), f.idcounts().size() * sizeof(f.idcounts()[0]));
            }
        }
    }
    auto t2 = std::chrono::high_resolution_clock::now();
    std::fprintf(logfp, "Subtract from the time to account for the parallel reduction/merging: %g\n", std::chrono::duration<double, std::milli>(t2 - t).count());
    std::fprintf(logfp, "min, max values are %0.20g, %0.20g\n", minv, maxv);
    size_t ind  = 0;
    std::vector<const char *> names {{"nibble", "uint8", "uint16", "uint32", "uint64"}};
    for(const auto sz: {15ul, 255ul, 65535ul, 0xFFFFFFFFul}) {
        auto optm = sketches->optimal_parameters(maxv, minv, sz);
        const char *s = names[ind];
        std::fprintf(logfp, "optimal a, b for %s are %0.22Lg and %0.22Lg", s, optm.second, optm.first);
        optm = sketches->optimal_parameters(maxv + 0.4, minv, sz);
        std::fprintf(logfp, ", or %0.22Lg and %0.22Lg, to get more information in general at the risk of saturation\n", optm.second, optm.first);
        ++ind;
    }
    OMP_PFOR
    for(int i = 0; i < nthreads; ++i) {
        kseq_destroy_stack(kseqs[i]);
        sketches[i].~SSType();
        encoders[i].~Encoder<>();
        rencoders[i].~RollingHasher<uint64_t>();
    }
    if(usketches) {
        OMP_PFOR
        for(int i = 0; i < nthreads; ++i)
            usketches[i].~SSType();
    }
    std::free(kseqs);
    std::free(sketches);
    std::free(usketches);
    std::free(encoders);
    std::free(rencoders);
    if(logfp != stderr) std::fclose(logfp);
    return EXIT_SUCCESS;
}
