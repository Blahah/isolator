
//#include <google/profiler.h>

#include <boost/accumulators/accumulators.hpp>
#include <boost/accumulators/statistics/mean.hpp>
#include <boost/accumulators/statistics/median.hpp>
#include <boost/accumulators/statistics/stats.hpp>
#include <boost/accumulators/statistics/variance.hpp>
#include <boost/foreach.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/thread.hpp>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <getopt.h>
#include <hdf5.h>
#include <hdf5_hl.h>
#include <map>
#include <sys/time.h>
#include <unistd.h>

#include "analyze.hpp"
#include "config.h"
#include "constants.hpp"
#include "fragment_model.hpp"
#include "linalg.hpp"
#include "logger.hpp"
#include "sampler.hpp"
#include "summarize.hpp"
#include "transcripts.hpp"
#include "gitversion.hpp"
#include "yaml/yaml.h"

using namespace boost::accumulators;

const char* isolator_logo =
"     _           _       _\n"
"    (_)___  ___ | | __ _| |_ ___  _ __\n"
"    | / __|/ _ \\| |/ _` | __/ _ \\| '__|\n"
"    | \\__ \\ (_) | | (_| | || (_) | |\n"
"    |_|___/\\___/|_|\\__,_|\\__\\___/|_|\n";


static void print_logo()
{
    printf("%s\n     Version: %s\n     Instruction set: %s\n\n",
           isolator_logo, GITVERSION, LINALG_INSTR_SET);
}


static void print_describe_usage(FILE* fout)
{
    fprintf(fout, "Usage: isolator describe isolator-output.h5\n");
}


static void print_describe_help(FILE* fout)
{
    print_describe_usage(fout);
    fprintf(fout,
            "\nOptions:\n"
            "-h, --help                Print this help message\n"
            "-o, --output=FILE         Write output to the given file (default: standard out)\n"
            "\n"
            "See 'isolator help summarize' for more.\n");
}


static int isolator_describe(int argc, char* argv[])
{
    Logger::start();

    static struct option long_options[] =
    {
        {"help",        no_argument,       NULL, 'h'},
        {"output",      required_argument, NULL, 'o'},
        {0, 0, 0, 0}
    };

    int opt;
    int opt_idx;
    const char* output_filename = NULL;

    while (true) {
        opt = getopt_long(argc, argv, "ho:", long_options, &opt_idx);

        if (opt == -1) break;

        switch (opt) {
            case 'h':
                print_describe_help(stdout);
                return 0;

            case 'o':
                output_filename = optarg;
                break;

            case '?':
                fprintf(stderr, "\n");
                print_describe_help(stderr);
                return 1;

            default:
                abort();
        }
    }

    /* no positional argumens */
    if (optind == argc) {
        print_describe_usage(stdout);
        return 0;
    }

    /* too many */
    else if (optind + 1 > argc) {
        fprintf(stderr, "Too many arguments.\n\n");
        print_describe_usage(stderr);
        return 1;
    }

    const char* input_filename = argv[optind];
    Summarize summarize(input_filename);

    FILE* output_file = stdout;
    if (output_filename) {
        output_file = fopen(output_filename, "w");
        if (!output_file) {
            Logger::abort("Can't open file %s for writing.\n", output_filename);
        }
    }

    IsolatorMetadata metadata;
    summarize.read_metadata(metadata);

    const std::vector<TranscriptID>& transcript_ids = summarize.get_transcript_ids();
    const std::vector<GeneID>& gene_ids = summarize.get_gene_ids();
    const std::vector<unsigned int>& tgroups = summarize.get_tgroups();

    std::set<GeneID> unique_gene_ids;
    unique_gene_ids.insert(gene_ids.begin(), gene_ids.end());

    std::set<unsigned int> unique_tgroups;
    unique_tgroups.insert(tgroups.begin(), tgroups.end());

    std::map<std::string, std::vector<std::string> > condition_samples;
    for (size_t i = 0; i < metadata.sample_filenames.size(); ++i) {
        condition_samples[metadata.sample_conditions[i]].push_back(
                metadata.sample_filenames[i]);
    }

    fprintf(output_file,
        "Experiment Design\n"
        "-----------------\n"
        "\n"
        "%lu samples in %lu conditions.\n",
        (unsigned long) metadata.sample_conditions.size(),
        (unsigned long) condition_samples.size());

    typedef std::map<std::string, std::vector<std::string> >::value_type
        condition_samples_value_type;
    BOOST_FOREACH (condition_samples_value_type& condition, condition_samples) {
        fprintf(output_file, "\nCondition %s:\n", condition.first.c_str());
        BOOST_FOREACH (std::string& filename, condition.second) {
            fprintf(output_file, "  %s\n", filename.c_str());
        }
    }

    fprintf(output_file,
        "\n\nAnalysis\n"
        "--------\n"
        "\n"
        "Gene annotations contained %lu transcripts in %lu genes and\n"
        "%lu transcription groups.\n"
        "\n"
        "Isolator version %s was run on %s taking %s seconds.\n"
        "\n"
        "Command line:\n  %s\n\n",
        (unsigned long) transcript_ids.size(),
        (unsigned long) unique_gene_ids.size(),
        (unsigned long) unique_tgroups.size(),
        metadata.version.c_str(),
        metadata.date.c_str(),
        metadata.elapsed_seconds.c_str(),
        metadata.command_line.c_str());

    return EXIT_SUCCESS;
}


static void print_summarize_usage(FILE* fout)
{
    fprintf(fout, "Usage: isolator summarize strategy [options] isolator-output.h5\n");
}


static void print_summarize_help(FILE* fout)
{
    print_summarize_usage(fout);
    fprintf(fout,
            "\nOptions:\n"
            "-h, --help                Print this help message\n"
            "-o, --out=FILE            Output summary to the given file. (default: standard out).\n"
            "-l, --list                List summarization strategies.\n"
            "\n"
            "See 'isolator help summarize' for more.\n");
}


static void print_summarize_strategies(FILE* fout)
{
    // TODO: I should really print descriptions of these
    fprintf(fout,
           "Available strategies:\n"
           "  transcript-expression\n"
           "  transcript-splicing\n"
           "  gene-expression\n"
           "  differential-transcription\n"
           "  differential-splicing\n"
           "  differential-feature-splicing\n"
           "  condition-splicing\n"
           "  condition-transcription\n"
           "  experiment-splicing-sigma\n"
           "  trace\n"
           "\n");
}


static int isolator_summarize(int argc, char* argv[])
{
    Logger::start();

    static struct option long_options[] =
    {
        {"help",         no_argument,       NULL, 'h'},
        {"out",          required_argument, NULL, 'o'},
        {"list",         no_argument,       NULL, 'l'},
        {"strategy",     required_argument, NULL, 's'},
        {"unnormalized", no_argument,       NULL, 'u'},
        {"credible",     required_argument, NULL, 'c'},
        {"effect-size",  required_argument, NULL, 'e'},
        {0, 0, 0, 0}
    };

    const char* out_filename = NULL;

    int opt;
    int opt_idx;
    double credible_interval = NAN;
    double minimum_effect_size = NAN;
    bool unnormalized = false;

    while (true) {
        opt = getopt_long(argc, argv, "ho:s:l", long_options, &opt_idx);

        if (opt == -1) break;

        switch (opt) {
            case 'h':
                print_summarize_help(stdout);
                return 0;

            case 'o':
                out_filename = optarg;
                break;

            case 'l':
                print_summarize_strategies(stdout);
                return 0;

            case 'u':
                unnormalized = true;
                break;

            case 'c':
                credible_interval = atof(optarg);
                break;

            case 'e':
                minimum_effect_size = atof(optarg);
                break;

            case '?':
                fprintf(stderr, "\n");
                print_summarize_help(stderr);
                return 1;

            default:
                abort();
        }
    }

    /* too few positional argumens */
    if (optind + 1 >= argc) {
        fprintf(stderr, "Too few arguments.\n\n");
        print_summarize_usage(stdout);
        return 0;
    }

    /* too many */
    else if (optind + 2 < argc) {
        fprintf(stderr, "Too many arguments.\n\n");
        print_summarize_usage(stderr);
        return 1;
    }

    char* strategy = argv[optind];
    const char* in_filename = argv[optind+1];

    // strategies are not case sensitive
    for (char* c = strategy; *c; ++c) {
        *c = tolower(*c);
        if (*c == '_') *c = '-';
    }

    std::string default_out_filename = std::string(strategy) + std::string(".tsv");

    FILE* out_file = NULL;
    if (!out_filename) out_filename = default_out_filename.c_str();
    out_file = fopen(out_filename, "w");
    if (out_file == NULL) {
        Logger::abort("Can't open file %s for writing.\n", out_filename);
    }


    Summarize summarize(in_filename);

    if (strcmp(strategy, "transcript-expression") == 0) {
        summarize.transcript_expression(out_file, credible_interval,
                                        unnormalized, false);
    }
    else if (strcmp(strategy, "transcript-splicing") == 0) {
        summarize.transcript_expression(out_file, credible_interval,
                                        unnormalized, true);
    }
    else if (strcmp(strategy, "gene-expression") == 0) {
        summarize.gene_expression(out_file, credible_interval,
                                  unnormalized);
    }
    else if (strcmp(strategy, "differential-transcription") == 0) {
        minimum_effect_size = log2(minimum_effect_size);
        summarize.differential_transcription(out_file, credible_interval,
                                             minimum_effect_size);
    }
    else if (strcmp(strategy, "differential-splicing") == 0) {
        summarize.differential_splicing(out_file, credible_interval,
                                        minimum_effect_size);
    }
    else if (strcmp(strategy, "differential-feature-splicing") == 0) {
        summarize.differential_feature_splicing(out_file, credible_interval,
                                                minimum_effect_size);
    }
    else if (strcmp(strategy, "condition-splicing") == 0) {
        summarize.condition_splicing(out_file, credible_interval);
    }
    else if (strcmp(strategy, "experiment-splicing-sigma") == 0) {
        summarize.experiment_splicing_sigma(out_file, credible_interval);
    }
    else if (strcmp(strategy, "experiment-splicing") == 0) {
        summarize.experiment_splicing(out_file, credible_interval);
    }
    else {
        fprintf(stderr, "No such summarization strategy: %s\n\n", strategy);
        print_summarize_strategies(stderr);
        return EXIT_FAILURE;
    }

    if (out_file != stdout) fclose(out_file);

    Logger::end();
    return EXIT_SUCCESS;
}


static void print_analyze_usage(FILE* fout)
{
    fprintf(fout,
            "Usage: isolator analyze [options] genes.gtf a1.bam[,a2.bam...] [b1.bam[,b2.bam...]]\n\n"
            "or\n\n"
            "       isolator analyze [options] genes.gtf experiment-description.yml\n");
}

static void print_analyze_help(FILE* fout)
{
    print_analyze_usage(fout);
    fprintf(fout,
        "\nOptions:\n"
        "-h, --help                Print this help message\n"
        "-o, --output=FILE         File to write HDF5 output to (default: isolator_output.hdf5)\n"
        "    --introns             Input consists of a BED file containing introns."
        "    --exons               Input consists of a BED file containing exons."
        "-v, --verbose             Print a bunch of information useful mainly for debugging\n"
        "-g, --genomic-seq=FILE    Correct for sequence bias, given the a the sequence\n"
        "                          against which the reads are aligned, in FAST format.\n"
        "-p, --threads=N           number of threads to use.\n"
        "    --no-gc-correction    disable post-hoc GC-content adjustments.\n"
        "-N, --num-samples         generate this many samples (default: 250)\n"
        "-B, --burnin              warmup for this many samples before collecting data (default: 100)\n\n"
        "See 'isolator help teste' for more.\n");
}


static void write_metadata(hid_t file_id, const IsolatorMetadata& metadata)
{
    hid_t dataspace = H5Screate(H5S_SCALAR);
    if (dataspace < 0) {
        Logger::abort("HDF5 dataspace creation failed.");
    }

    hid_t varstring_type = H5Tcopy(H5T_C_S1);
    if (varstring_type < 0 || H5Tset_size(varstring_type, H5T_VARIABLE) < 0) {
        Logger::abort("HDF5 type creation failed.");
    }

    hid_t group = H5Gcreate1(file_id, "/metadata", 0);
    if (group < 0) {
        Logger::abort("HDF5 group creation failed.");
    }

    hid_t attr;
    const char* attr_value;

    attr = H5Acreate1(group, "command_line", varstring_type, dataspace, H5P_DEFAULT);
    attr_value = metadata.command_line.c_str();
    H5Awrite(attr, varstring_type, &attr_value);
    H5Aclose(attr);

    attr = H5Acreate1(group, "version", varstring_type, dataspace, H5P_DEFAULT);
    attr_value = metadata.version.c_str();
    H5Awrite(attr, varstring_type, &attr_value);
    H5Aclose(attr);

    attr = H5Acreate1(group, "date", varstring_type, dataspace, H5P_DEFAULT);
    attr_value = metadata.date.c_str();
    H5Awrite(attr, varstring_type, &attr_value);
    H5Aclose(attr);

    attr = H5Acreate1(group, "elapsed_seconds", varstring_type, dataspace, H5P_DEFAULT);
    attr_value = metadata.elapsed_seconds.c_str();
    H5Awrite(attr, varstring_type, &attr_value);
    H5Aclose(attr);

    hsize_t sample_dataspace_dims[1] = {metadata.sample_filenames.size()};
    hid_t sample_dataspace = H5Screate_simple(1, sample_dataspace_dims, NULL);
    if (sample_dataspace < 0) {
        Logger::abort("HDF5 dataspace creation failed.");
    }

    attr = H5Acreate1(group, "sample_filenames", varstring_type,
                      sample_dataspace, H5P_DEFAULT);
    const char** sample_attr_value = new const char*[metadata.sample_filenames.size()];
    for (size_t i = 0; i < metadata.sample_filenames.size(); ++i) {
        sample_attr_value[i] = metadata.sample_filenames[i].c_str();
    }
    H5Awrite(attr, varstring_type, sample_attr_value);
    H5Aclose(attr);

    attr = H5Acreate1(group, "sample_names", varstring_type,
                      sample_dataspace, H5P_DEFAULT);
    for (size_t i = 0; i < metadata.sample_names.size(); ++i) {
        sample_attr_value[i] = metadata.sample_names[i].c_str();
    }
    H5Awrite(attr, varstring_type, sample_attr_value);
    H5Aclose(attr);

    attr = H5Acreate1(group, "sample_conditions", varstring_type,
                      sample_dataspace, H5P_DEFAULT);
    for (size_t i = 0; i < metadata.sample_conditions.size(); ++i) {
        sample_attr_value[i] = metadata.sample_conditions[i].c_str();
    }
    H5Awrite(attr, varstring_type, sample_attr_value);
    H5Aclose(attr);

    delete [] sample_attr_value;
    H5Sclose(sample_dataspace);
    H5Gclose(group);
    H5Tclose(varstring_type);
    H5Sclose(dataspace);
}


// Write a table with cassette exon information, which should look something
// like:
//   seqname, start, end, strand, inclusion_tids, exclusion_tids
//
static void write_gene_features(hid_t file_id, TranscriptSet& ts)
{
    std::vector<Interval> cassette_exons;
    std::vector<std::vector<unsigned int> > cass_including_tids, cass_excluding_tids;
    ts.get_cassette_exons(cassette_exons, cass_including_tids, cass_excluding_tids);
    Logger::info("%lu cassette exons", (unsigned long) cassette_exons.size());

    std::vector<Interval> retained_introns;
    std::vector<std::vector<unsigned int> > retin_including_tids, retin_excluding_tids;
    ts.get_retained_introns(retained_introns, retin_including_tids, retin_excluding_tids);
    Logger::info("%lu retained introns", (unsigned long) retained_introns.size());

    std::vector<Interval> feature_intervals;
    std::vector<std::vector<unsigned int> > including_tids, excluding_tids;
    std::vector<unsigned int> feature_type;

    feature_intervals.insert(feature_intervals.end(),
                             cassette_exons.begin(),
                             cassette_exons.end());
    including_tids.insert(including_tids.end(),
                          cass_including_tids.begin(),
                          cass_including_tids.end());
    excluding_tids.insert(excluding_tids.end(),
                          cass_excluding_tids.begin(),
                          cass_excluding_tids.end());
    for (size_t i = 0; i < cassette_exons.size(); ++i) {
        feature_type.push_back((unsigned int) GENE_FEATURE_CASSETTE_EXON);
    }

    feature_intervals.insert(feature_intervals.end(),
                             retained_introns.begin(),
                             retained_introns.end());
    including_tids.insert(including_tids.end(),
                          retin_including_tids.begin(),
                          retin_including_tids.end());
    excluding_tids.insert(excluding_tids.end(),
                          retin_excluding_tids.begin(),
                          retin_excluding_tids.end());
    for (size_t i = 0; i < cassette_exons.size(); ++i) {
        feature_type.push_back((unsigned int) GENE_FEATURE_RETAINED_INTRON);
    }

    size_t n = feature_intervals.size();

    herr_t status;

    // create types for each field
    hid_t varstring_type = H5Tcopy(H5T_C_S1);
    if (varstring_type < 0 || H5Tset_size(varstring_type, H5T_VARIABLE) < 0) {
        Logger::abort("HDF5 type creation failed.");
    }

    hid_t tids_type = H5Tvlen_create(H5T_NATIVE_UINT);
    if (tids_type < 0) {
        Logger::abort("HDF5 type creation failed.");
    }

    if (H5Gcreate1(file_id, "/features", 0) < 0) {
        Logger::abort("HDF5 group creation failed.");
    }

    hsize_t dims[1] = { n };
    hid_t feature_dataspace = H5Screate_simple_checked(1, dims, NULL);

    hid_t dataset_create_property = H5Pcreate(H5P_DATASET_CREATE);
    H5Pset_layout(dataset_create_property, H5D_CHUNKED);
    H5Pset_chunk(dataset_create_property, 1, dims);
    H5Pset_deflate(dataset_create_property, 7);

    // write sequence names
    hid_t seqname_id = H5Dcreate2_checked(file_id, "/features/seqname",
                                          varstring_type, feature_dataspace,
                                          H5P_DEFAULT, dataset_create_property,
                                          H5P_DEFAULT);

    char** string_data = new char* [n];
    for (size_t i = 0; i < n; ++i) {
        const std::string& seqname = feature_intervals[i].seqname.get();
        string_data[i] = new char[seqname.size() + 1];
        memcpy(string_data[i], seqname.c_str(), seqname.size() + 1);
    }

    status = H5Dwrite(seqname_id, varstring_type, H5S_ALL, H5S_ALL,
                      H5P_DEFAULT, string_data);
    if (status < 0) Logger::abort("HDF5 write failed.");

    for (size_t i = 0; i < n; ++i) {
        delete [] string_data[i];
    }
    delete [] string_data;

    H5Dclose(seqname_id);

    // write start positions
    hid_t start_id = H5Dcreate2_checked(file_id, "/features/start",
                                        H5T_NATIVE_LONG, feature_dataspace,
                                        H5P_DEFAULT, dataset_create_property,
                                        H5P_DEFAULT);


    long* long_data = new long [n];
    for (size_t i = 0; i < n; ++i) {
        long_data[i] = feature_intervals[i].start;
    }

    H5Dwrite_checked(start_id, H5T_NATIVE_LONG, H5S_ALL, H5S_ALL,
                     H5P_DEFAULT, long_data);
    H5Dclose(start_id);

    // write end positions
    hid_t end_id = H5Dcreate2_checked(file_id, "/features/end",
                                      H5T_NATIVE_LONG, feature_dataspace,
                                      H5P_DEFAULT, dataset_create_property,
                                      H5P_DEFAULT);

    for (size_t i = 0; i < n; ++i) {
        long_data[i] = feature_intervals[i].end;
    }

    H5Dwrite_checked(end_id, H5T_NATIVE_LONG, H5S_ALL, H5S_ALL,
                     H5P_DEFAULT, long_data);
    H5Dclose(end_id);
    delete[] long_data;

    // write feature type
    hid_t type_id = H5Dcreate2_checked(file_id, "/features/type",
                                       H5T_NATIVE_UINT, feature_dataspace,
                                       H5P_DEFAULT, dataset_create_property,
                                       H5P_DEFAULT);

    H5Dwrite_checked(type_id, H5T_NATIVE_UINT, H5S_ALL, H5S_ALL,
                     H5P_DEFAULT, &feature_type.at(0));
    H5Dclose(type_id);

    // write strands
    hid_t strand_id = H5Dcreate2_checked(file_id, "/features/strand",
                                         H5T_NATIVE_CHAR, feature_dataspace,
                                         H5P_DEFAULT, dataset_create_property,
                                         H5P_DEFAULT);

    char* char_data = new char[n];
    for (size_t i = 0; i < n; ++i) {
        if (feature_intervals[i].strand == strand_pos) {
            char_data[i] = '+';
        }
        else if (feature_intervals[i].strand == strand_neg) {
            char_data[i] = '-';
        }
        else {
            char_data[i] = '.';
        }
    }

    H5Dwrite_checked(strand_id, H5T_NATIVE_CHAR, H5S_ALL, H5S_ALL,
                     H5P_DEFAULT, char_data);

    delete [] char_data;
    H5Dclose(strand_id);

    // TODO: write feature type

    // write including tids
    hid_t including_tids_id = H5Dcreate2_checked(file_id, "/features/including_tid",
                                                 tids_type, feature_dataspace,
                                                 H5P_DEFAULT, dataset_create_property,
                                                 H5P_DEFAULT);

    hvl_t* vlen_data = new hvl_t [n];
    for (size_t i = 0; i < n; ++i) {
        vlen_data[i].len = including_tids[i].size();
        unsigned int* ptids = new unsigned int [including_tids[i].size()];
        for (size_t j = 0; j < including_tids[i].size(); ++j) {
            ptids[j] = including_tids[i][j];
        }
        vlen_data[i].p = reinterpret_cast<void*>(ptids);
    }

    H5Dwrite_checked(including_tids_id, tids_type, H5S_ALL, H5S_ALL,
                     H5P_DEFAULT, vlen_data);
    H5Dclose(including_tids_id);

    // write excluding tids
    hid_t excluding_tids_id = H5Dcreate2_checked(file_id, "/features/excluding_tid",
                                                 tids_type, feature_dataspace,
                                                 H5P_DEFAULT, dataset_create_property,
                                                 H5P_DEFAULT);

    for (size_t i = 0; i < n; ++i) {
        vlen_data[i].len = excluding_tids[i].size();
        unsigned int* ptids = new unsigned int [excluding_tids[i].size()];
        for (size_t j = 0; j < excluding_tids[i].size(); ++j) {
            ptids[j] = excluding_tids[i][j];
        }
        delete [] reinterpret_cast<unsigned int*>(vlen_data[i].p);
        vlen_data[i].p = reinterpret_cast<void*>(ptids);
    }

    H5Dwrite_checked(excluding_tids_id, tids_type, H5S_ALL, H5S_ALL,
                     H5P_DEFAULT, vlen_data);
    H5Dclose(excluding_tids_id);

    for (size_t i = 0; i < n; ++i) {
        delete [] reinterpret_cast<unsigned int*>(vlen_data[i].p);
    }
    delete [] vlen_data;

    H5Tclose(varstring_type);
    H5Tclose(tids_type);
}


static std::string yaml_event_name(yaml_event_type_t type)
{
    switch (type) {
        case YAML_STREAM_START_EVENT:
            return "stream start";
        case YAML_STREAM_END_EVENT:
            return "stream end";
        case YAML_DOCUMENT_START_EVENT:
            return "document start";
        case YAML_DOCUMENT_END_EVENT:
            return "document end";
        case YAML_ALIAS_EVENT:
            return "alias";
        case YAML_SCALAR_EVENT:
            return "scalar";
        case YAML_SEQUENCE_START_EVENT:
            return "sequence start";
        case YAML_SEQUENCE_END_EVENT:
            return "sequence end";
        case YAML_MAPPING_START_EVENT:
            return "mapping start";
        case YAML_MAPPING_END_EVENT:
            return "mapping end";
        default:
            return "unknown event";
    }
}


static void parse_experiment_description_error(
        const char* filename, yaml_event_type_t expected, yaml_event_type_t observed)
{
    Logger::abort("Error parsing %s: expected %s while parsing %s.",
                  filename,
                  yaml_event_name(expected).c_str(),
                  yaml_event_name(observed).c_str());
}


// Parse a YAML file describing the experiment
static void parse_experiment_description(const char* filename,
                                         std::vector<std::string>& condition_names,
                                         std::vector<std::string>& sample_names,
                                         std::vector<std::string>& sample_filenames)
{
    FILE* input = fopen(filename, "rb");
    if (!input) {
        Logger::abort("Unable to open experiment description file %s.",
                      filename);
    }

    yaml_parser_t parser;
    yaml_parser_initialize(&parser);
    yaml_parser_set_input_file(&parser, input);

    std::string current_condition_name;
    int condition_sample_number = 0;

    enum {
        STATE_BEGIN,
        STATE_END,
        STATE_CONDITION_MAP,
        STATE_SAMPLES,
        STATE_SAMPLE_SEQUENCE,
        STATE_SAMPLE_MAPPING,
        STATE_SAMPLE_FILENAME
    } state = STATE_BEGIN;

    yaml_event_t event;
    bool done = false;
    while (!done) {
        if (!yaml_parser_parse(&parser, &event)) {
            Logger::abort("Error parsing YAML file %s", filename);
        }

        switch (state) {
            case STATE_BEGIN:
                if (event.type == YAML_MAPPING_START_EVENT) {
                    state = STATE_CONDITION_MAP;
                }
                else if (event.type == YAML_STREAM_START_EVENT ||
                         event.type == YAML_DOCUMENT_START_EVENT) {
                }
                else {
                    parse_experiment_description_error(
                        filename, YAML_MAPPING_START_EVENT, event.type);
                }
                break;

            case STATE_CONDITION_MAP:
                if (event.type == YAML_SCALAR_EVENT) {
                    current_condition_name =
                        reinterpret_cast<char*>(event.data.scalar.value);
                    condition_sample_number = 0;
                    state = STATE_SAMPLES;
                }
                else if (event.type == YAML_MAPPING_END_EVENT) {
                    state = STATE_END;
                }
                else {
                    parse_experiment_description_error(
                        filename, YAML_SCALAR_EVENT, event.type);
                }
                break;

            case STATE_SAMPLES:
                if (event.type == YAML_SEQUENCE_START_EVENT) {
                    state = STATE_SAMPLE_SEQUENCE;
                }
                else if (event.type == YAML_MAPPING_START_EVENT) {
                    state = STATE_SAMPLE_MAPPING;
                }
                else {
                    parse_experiment_description_error(
                        filename, YAML_MAPPING_START_EVENT, event.type);
                }
                break;

            case STATE_SAMPLE_SEQUENCE:
                if (event.type == YAML_SCALAR_EVENT) {
                    condition_names.push_back(current_condition_name);
                    sample_names.push_back(
                            current_condition_name + "-" +
                            boost::lexical_cast<std::string>(condition_sample_number + 1));
                    sample_filenames.push_back(
                            reinterpret_cast<char*>(event.data.scalar.value));
                }
                else if (event.type == YAML_SEQUENCE_END_EVENT) {
                    state = STATE_CONDITION_MAP;
                }
                else {
                    parse_experiment_description_error(
                        filename, YAML_SCALAR_EVENT, event.type);
                }
                break;

            case STATE_SAMPLE_MAPPING:
                if (event.type == YAML_SCALAR_EVENT) {
                    condition_names.push_back(current_condition_name);
                    sample_names.push_back(
                            reinterpret_cast<char*>(event.data.scalar.value));
                    state = STATE_SAMPLE_FILENAME;
                }
                else if (event.type == YAML_MAPPING_END_EVENT) {
                    state = STATE_CONDITION_MAP;
                }
                else {
                    parse_experiment_description_error(
                        filename, YAML_SCALAR_EVENT, event.type);
                }
                break;

            case STATE_SAMPLE_FILENAME:
                if (event.type == YAML_SCALAR_EVENT) {
                    sample_filenames.push_back(
                            reinterpret_cast<char*>(event.data.scalar.value));
                    state = STATE_SAMPLE_MAPPING;
                }
                else {
                    parse_experiment_description_error(
                        filename, YAML_SCALAR_EVENT, event.type);
                }
                break;

            case STATE_END:
                if (event.type == YAML_DOCUMENT_END_EVENT) {
                    done = true;
                }
                else {
                    parse_experiment_description_error(
                        filename, YAML_DOCUMENT_END_EVENT, event.type);
                }
                break;

            default:
                Logger::abort("Unknown experiment description parser state.");
        }

        yaml_event_delete(&event);
    }

    yaml_parser_delete(&parser);
    fclose(input);
}


static void parse_command_line_experiment_description(
        char** argv, int argc,
       std::vector<std::string>& condition_names,
       std::vector<std::string>& sample_names,
       std::vector<std::string>& sample_filenames)
{
    char condition_name[100];
    int condition_num = 1;
    for (int i = 0; i < argc; ++i) {
        snprintf(condition_name, sizeof(condition_name), "condition-%d", condition_num);

        const char* fn;
        int sample_num = 1;
        for (fn = strtok(argv[i], ","); fn; fn = strtok(NULL, ",")) {
            condition_names.push_back(condition_name);
            sample_names.push_back(
                    std::string(condition_name) + "-" +
                    boost::lexical_cast<std::string>(sample_num));
            sample_filenames.push_back(fn);
            ++sample_num;
        }

        ++condition_num;
    }
}


static bool is_sam_file(const char* filename)
{
    samfile_t* sam_file = samopen(filename, "rb", NULL);
    if (!sam_file || !sam_file->header || sam_file->header->n_targets == 0) {
        if (sam_file) samclose(sam_file);

        sam_file = samopen(filename, "r", NULL);
        if (!sam_file || !sam_file->header || sam_file->header->n_targets == 0) {
            return false;
        }
        else return true;
    }
    else return true;
}


static int isolator_analyze(int argc, char* argv[])
{
    static struct option long_options[] =
    {
        {"help",                 no_argument,       NULL, 'h'},
        {"output",               required_argument, NULL, 'o'},
        {"introns",              no_argument,       NULL, 0},
        {"exons",                no_argument,       NULL, 0},
        {"verbose",              no_argument,       NULL, 'v'},
        {"genomic-seq",          required_argument, NULL, 'g'},
        {"threads",              required_argument, NULL, 'p'},
        {"no-gc-correction",     no_argument,       NULL, 0},
        {"num-samples",          required_argument, NULL, 'N'},
        {"burnin",               required_argument, NULL, 'B'},
        {"tss-cluster-distance", required_argument, NULL, 0},
        {0, 0, 0, 0}
    };

    Logger::level logger_level = Logger::INFO;
    unsigned int burnin = 100;
    unsigned int num_samples = 250;
    pos_t tss_cluster_dist = 150;
    bool run_gc_correction = true;
    constants::num_threads = boost::thread::hardware_concurrency();
    const char* fa_fn  = NULL;
    const char* output_filename = "isolator-output.h5";
    bool use_introns = false;
    bool use_exons = false;

    int opt;
    int optidx;
    while (true) {
        opt = getopt_long(argc, argv, "ho:vg:p:N:", long_options, &optidx);

        if (opt == -1) break;

        switch (opt) {
            case 'h':
                print_analyze_help(stdout);
                return 0;

            case 'o':
                output_filename = optarg;
                break;

            case 'v':
                logger_level = Logger::DEBUG;
                break;

            case 'p':
                constants::num_threads = std::max(1, atoi(optarg));
                break;

            case 'g':
                fa_fn = optarg;
                break;

            case 'B':
                burnin = strtoul(optarg, NULL, 10);
                break;

            case 'N':
                num_samples = strtoul(optarg, NULL, 10);
                break;

            case 0:
                if (strcmp(long_options[optidx].name, "no-gc-correction") == 0) {
                    run_gc_correction = false;
                }
                else if (strcmp(long_options[optidx].name, "tss-cluster-distance") == 0) {
                    tss_cluster_dist = strtod(optarg, NULL);
                }
                else if (strcmp(long_options[optidx].name, "introns") == 0) {
                    use_introns = true;
                }
                else if (strcmp(long_options[optidx].name, "exons") == 0) {
                    use_exons = true;
                }
                break;

            case '?':
                fprintf(stderr, "\n");
                print_analyze_help(stderr);
                return 1;

            default:
                abort();
        }
    }

    // no positional argumens
    if (optind + 1 >= argc) {
        print_analyze_usage(stdout);
        return 0;
    }

    print_logo();
    Logger::start();
    Logger::set_level(logger_level);

    // read transcripts
    const char* annotation_filename = argv[optind++];
    TranscriptSet ts;

    // TODO: parse a; bed file if the right option is set
    if (use_introns && use_exons) {
        Logger::abort("'--introns' and '--exons' arguments are mutually exclusize.");
    }
    else if (use_introns) {
        ts.read_bed(annotation_filename);
    }
    else if (use_exons) {
        Logger::abort("Reading exons from BED files is not yet supported.");
    }
    else {
        ts.read_gtf(annotation_filename, tss_cluster_dist);
    }

    hid_t output_file_id = H5Fcreate(output_filename, H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT);
    if (output_file_id < 0) {
        Logger::abort("Unable to open %s for writing.", output_filename);
    }

    // write metadata
    write_gene_features(output_file_id, ts);

    IsolatorMetadata metadata;

    bool has_experiment_description = false;

    if (optind + 1 == argc) {
        const char* filename = argv[optind];
        if (!is_sam_file(filename)) has_experiment_description = true;
    }

    if (has_experiment_description) {
        parse_experiment_description(argv[optind],
                                     metadata.sample_conditions,
                                     metadata.sample_names,
                                     metadata.sample_filenames);
    }
    else {
        parse_command_line_experiment_description(
                argv + optind, argc - optind,
                metadata.sample_conditions,
                metadata.sample_names,
                metadata.sample_filenames);
    }

    Analyze analyze(burnin, num_samples, ts, fa_fn, run_gc_correction);
    for (size_t i = 0; i < metadata.sample_conditions.size(); ++i) {
        analyze.add_sample(metadata.sample_conditions[i].c_str(),
                           metadata.sample_filenames[i].c_str());
    }

    boost::timer::cpu_timer timer;
    timer.start();

    analyze.run(output_file_id);

    boost::timer::cpu_times elapsed_time = timer.elapsed();

    char elapsed_seconds_string[100];
    snprintf(elapsed_seconds_string, 100, "%0.1f", (double) elapsed_time.wall / 1e9);
    metadata.elapsed_seconds = elapsed_seconds_string;

    metadata.command_line = "isolator";
    for (int i = 0; i < argc; ++i) {
        metadata.command_line += " ";
        metadata.command_line += argv[i];
    }

    metadata.version = GITVERSION;

    time_t t = time(NULL);
    struct tm timestruct;
    localtime_r(&t, &timestruct);
    char time_string[200];
    strftime(time_string, 200, "%a, %d %b %Y %T %z", &timestruct);
    metadata.date = time_string;

    write_metadata(output_file_id, metadata);
    Logger::info("Finished. Have a nice day!");
    Logger::end();

    return EXIT_SUCCESS;
}


static void print_trace_usage(FILE* fout)
{
    fprintf(fout, "Usage: isolator trace transcript_id isolator-output.h5\n");
}


static void print_trace_help(FILE* fout)
{
    print_trace_usage(fout);
    fprintf(fout,
        "\nOptions:\n"
        "-h, --help                Print this help message\n"
        "-o, --out=FILE            Output summary to the given file. (default: standard out).\n" );
}


static int isolator_trace(int argc, char* argv[])
{
    // TODO: we should add an option to specify wether we are output a trace
    // of the splicing parameters or transcription parameters. Right now I'm just
    // doing splicing because thats what I need to debug.

    Logger::start();

    static struct option long_options[] =
    {
        {"help", no_argument,       NULL, 'h'},
        {"out",  required_argument, NULL, 'o'},
        {0, 0, 0, 0}
    };

    const char* out_filename = NULL;
    int opt;
    int opt_idx;

    while (true) {
        opt = getopt_long(argc, argv, "ho:", long_options, &opt_idx);
        if (opt == -1) break;

        switch (opt) {
            case 'h':
                print_summarize_help(stdout);
                return 0;

            case 'o':
                out_filename = optarg;
                break;

            case '?':
                fprintf(stderr, "\n");
                print_trace_help(stderr);
                return 1;

            default:
                abort();
        }

    }

    if (optind + 1 >= argc) {
        fprintf(stderr, "Too few arguments.\n\n");
        print_trace_usage(stdout);
        return 0;
    }

    if (optind + 2 < argc) {
        fprintf(stderr, "Too many arguments.\n\n");
        return 1;
    }

    const char* transcript_id = argv[optind];
    const char* in_filename = argv[optind+1];

    std::string default_out_filename = std::string(transcript_id) + std::string("-trace.tsv");

    FILE* out_file = NULL;
    if (!out_filename) out_filename = default_out_filename.c_str();
    out_file = fopen(out_filename, "w");
    if (out_file == NULL) {
        Logger::abort("Can't open file %s for writing.\n", out_filename);
    }

    Summarize summarize(in_filename);

    std::vector<float> ex_mu =
        summarize.transcript_experiment_splicing(transcript_id);
    std::vector<std::vector<float> > cond_mus =
        summarize.transcript_condition_splicing(transcript_id);

    size_t num_samples = ex_mu.size();
    size_t C = cond_mus.size();
    fprintf(out_file, "round\tex_mu");
    for (size_t i = 0; i < C; ++i) {
        fprintf(out_file, "\tmu%lu", (unsigned long) i);
    }
    fputc('\n', out_file);

    for (size_t i = 0; i < num_samples; ++i) {
        fprintf(out_file, "%lu\t%f", (unsigned long) i + 1, ex_mu[i]);
        for (size_t j = 0; j < C; ++j) {
            fprintf(out_file, "\t%f", cond_mus[j][i]);
        }
        fputc('\n', out_file);
    }

    if (out_file != stdout) fclose(out_file);

    Logger::end();
    return EXIT_SUCCESS;
}



static void print_usage(FILE* fout)
{
    fprintf(fout,
            "Usage: isolator <command> [<args>]\n"
            "Isolator version %s\n"
            "Instruction set: %s\n\n"
            "Where <command> is one of:\n"
            "    analyze           Quantify and test for differential expression\n"
            "                      and splicing, among other things.\n"
            "    summarize         Summarize a sampler run.\n"
            "    describe          Briefly describe contents of an output file.\n"
            "    help              Become enlightened.\n",
            GITVERSION, LINALG_INSTR_SET);
}


static int isolator_help(int argc, char* argv[])
{
    if (argc <= 1) {
        print_usage(stdout);
        return EXIT_SUCCESS;
    }

    size_t cmdlen = 9 + strlen(argv[1]) + 1;
    char* cmd = new char [cmdlen];
    snprintf(cmd, cmdlen, "isolator-%s", argv[1]);

    execlp("man", "man", cmd, (char*) NULL);

    // if execlp returned, there was an error
    fprintf(stderr, "No suitable man page viewer found.\n");
    return EXIT_FAILURE;
}


int main(int argc, char* argv[])
{
    linalg_init();

    if (argc <= 1) {
        print_usage(stdout);
        return EXIT_SUCCESS;
    }

    ++argv;
    --argc;

    if (strcmp(argv[0], "-h") == 0 || strcmp(argv[0], "--help") == 0) {
        return isolator_help(argc, argv);
    }

    if (strcmp(argv[0], "summarize") == 0) {
        return isolator_summarize(argc, argv);
    }
    else if (strcmp(argv[0], "describe") == 0) {
        return isolator_describe(argc, argv);
    }
    else if (strcmp(argv[0], "analyze") == 0) {
        return isolator_analyze(argc, argv);
    }
    else if (strcmp(argv[0], "help") == 0) {
        return isolator_help(argc, argv);
    }
    else if (strcmp(argv[0], "trace") == 0) {
        return isolator_trace(argc, argv);
    }

    fprintf(stderr, "Unknown command %s.\n\n", argv[0]);
    return EXIT_FAILURE;
}
