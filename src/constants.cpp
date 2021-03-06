
#include "constants.hpp"

unsigned int         constants::num_threads                  = 1;
unsigned int         constants::min_map_qual                 = 0;
size_t               constants::max_estimate_queue_size      = 500;
pos_t                constants::min_estimate_exon_length     = 200;
pos_t                constants::seqbias_min_exon_length      = 200;
size_t               constants::seqbias_num_collected_reads  = 500000;
size_t               constants::seqbias_num_reads            = 50000;
size_t               constants::seqbias_left_pos             = 15;
size_t               constants::seqbias_right_pos            = 15;
pos_t                constants::seqbias_tp_end               = 0;
pos_t                constants::seqbias_fp_end               = 0;
double               constants::gc_loess_smoothing           = 1.5;
constants::libtype_t constants::libtype                      = constants::LIBTYPE_FR;
unsigned int         constants::max_alignments               = 100;
pos_t                constants::max_frag_len                 = 1000;
float                constants::min_frag_len_pr              = 1e-3;
float                constants::transcript_len_min_frag_pr   = 1e-3;
float                constants::frag_len_dist_smoothing      = 0.2;
size_t               constants::frag_len_min_pe_reads        = 10000;
double               constants::frag_len_mu                  = 200.0;
double               constants::frag_len_sd                  = 20.0;
float                constants::min_frag_weight              = 1e-5;
float                constants::zero_eps                     = 1e-13;
pos_t                constants::alt_exon_flank_length        = 250;
pos_t                constants::transcript_5p_extension      = 0;
pos_t                constants::transcript_3p_extension      = 0;
float                constants::min_transcript_weight        = 1.0;
float                constants::min_transcript_fraglen_acceptance = 0.5;
float                constants::tmix_prior_prec              = 0.01;
unsigned int         constants::sampler_component_block_size = 50;
unsigned int         constants::sampler_multiread_block_size = 10000;
float                constants::maxpost_rel_error            = 0.001;
float                constants::maxpost_abs_error            = 1e-12;
float                constants::maxpost_abs_peps             = 1.0;
unsigned int         constants::max_newton_iter              = 12;
unsigned int         constants::sampler_burnin_samples       = 200;
unsigned int         constants::sampler_hillclimb_samples    = 0;
unsigned int         constants::min_tss_group_isoforms_conditioning = 50;
double               constants::sample_scaling_quantile      = 0.9;
size_t               constants::sample_scaling_truncation    = 100000;

