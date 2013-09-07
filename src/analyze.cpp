
#include <boost/foreach.hpp>
#include <boost/numeric/ublas/matrix_proxy.hpp>
#include <boost/thread.hpp>
#include <gsl/gsl_statistics_double.h>

#include "analyze.hpp"
#include "constants.hpp"
#include "shredder.hpp"

using namespace boost::numeric::ublas;


#if 0
class TgroupMuSampler : public Shredder
{
    public:
        TgroupMuSampler()
            : Sampler(-INFINITY, INFINITY)
        {
        }

    protected:
        double f(double x, double&d)
        {
            double fx = 0;
            d = 0;
        }

    private:
        double sigma, double;

};
#endif


Analyze::Analyze(size_t burnin,
                 size_t num_samples,
                 TranscriptSet& transcripts,
                 const char* genome_filename,
                 bool run_gc_correction)
    : burnin(burnin)
    , num_samples(num_samples)
    , transcripts(transcripts)
    , genome_filename(genome_filename)
    , run_gc_correction(run_gc_correction)
    , K(0)
    , C(0)
    , N(0)
    , T(0)
{
    N = transcripts.size();
    T = transcripts.num_tgroups();

    // TODO: constants (also maybe command line options eventually)
    tgroup_nu = 4.0;
    tgroup_alpha_alpha = 5.0;
    tgroup_beta_alpha  = 2.5;
    tgroup_alpha_beta  = 5.0;
    tgroup_beta_beta   = 2.5;

    tgroup_expr.resize(T);

    Logger::info("Number of transcripts: %u", N);
    Logger::info("Number of transcription groups: %u", T);
}


Analyze::~Analyze()
{

}


void Analyze::add_sample(const char* condition_name, const char* filename)
{
    int c;
    std::map<std::string, int>::iterator it = condition_index.find(condition_name);
    if (it == condition_index.end()) {
        c = (int) condition_index.size();
        condition_index[condition_name] = c;
    }
    else c = (int) it->second;

    filenames.push_back(filename);
    condition.push_back(c);
    ++K;
}


// Thread to initialize samplers and fragment models
class SamplerInitThread
{
    public:
        SamplerInitThread(const std::vector<std::string>& filenames, const char* fa_fn,
                          TranscriptSet& transcripts,
                          std::vector<FragmentModel*>& fms,
                          bool run_gc_correction,
                          std::vector<Sampler*>& samplers,
                          Queue<int>& indexes)
            : filenames(filenames)
            , fa_fn(fa_fn)
            , transcripts(transcripts)
            , fms(fms)
            , run_gc_correction(run_gc_correction)
            , samplers(samplers)
            , indexes(indexes)
            , thread(NULL)
        {
        }

        void run()
        {
            int index;
            while (true) {
                if ((index = indexes.pop()) == -1) break;

                fms[index] = new FragmentModel();
                fms[index]->estimate(transcripts, filenames[index].c_str(), fa_fn);

                samplers[index] = new Sampler(filenames[index].c_str(), fa_fn,
                                              transcripts, *fms[index], run_gc_correction,
                                              // XXX
                                              false);
            }
        }

        void start()
        {
            if (thread != NULL) return;
            thread = new boost::thread(boost::bind(&SamplerInitThread::run, this));
        }

        void join()
        {
            thread->join();
            delete thread;
            thread = NULL;
        }

    private:
        const std::vector<std::string>& filenames;
        const char* fa_fn;
        TranscriptSet& transcripts;
        std::vector<FragmentModel*>& fms;
        bool run_gc_correction;

        std::vector<Sampler*>& samplers;

        Queue<int>& indexes;

        boost::thread* thread;
};


// Threads to run sampler iterations
class SamplerTickThread
{
    public:
        SamplerTickThread(std::vector<Sampler*>& samplers,
                          matrix<double>& Q,
                          Queue<int>& tick_queue,
                          Queue<int>& tock_queue)
            : samplers(samplers)
            , Q(Q)
            , tick_queue(tick_queue)
            , tock_queue(tock_queue)
            , thread(NULL)
        { }

        void run()
        {
            int index;
            while (true) {
                if ((index = tick_queue.pop()) == -1) break;
                samplers[index]->transition();
                const std::vector<double>& state = samplers[index]->state();

                matrix_row<matrix<double> > row(Q, index);
                std::copy(state.begin(), state.end(), row.begin());

                // notify of completion
                tock_queue.push(0);
            }
        }

        void start()
        {
            if (thread != NULL) return;
            thread = new boost::thread(boost::bind(&SamplerTickThread::run, this));
        }

        void join()
        {
            thread->join();
            delete thread;
            thread = NULL;
        }

    private:
        std::vector<Sampler*> samplers;
        matrix<double>& Q;
        Queue<int>& tick_queue;
        Queue<int>& tock_queue;
        boost::thread* thread;

};


void Analyze::setup()
{
    fms.resize(K);
    qsamplers.resize(K);

    std::vector<SamplerInitThread*> threads(constants::num_threads);
    Queue<int> indexes;
    for (unsigned int i = 0; i < constants::num_threads; ++i) {
        threads[i] = new SamplerInitThread(filenames, genome_filename, transcripts,
                                           fms, run_gc_correction, qsamplers,
                                           indexes);
        threads[i]->start();
    }

    for (unsigned int i = 0; i < K; ++i) indexes.push(i);
    for (unsigned int i = 0; i < constants::num_threads; ++i) indexes.push(-1);

    for (unsigned int i = 0; i < constants::num_threads; ++i) {
        threads[i]->join();
        delete threads[i];
    }
}


void Analyze::cleanup()
{
    BOOST_FOREACH (FragmentModel* fm, fms) {
        delete fm;
    }
    fms.clear();

    BOOST_FOREACH (Sampler* qs, qsamplers) {
        delete qs;
    }
    qsamplers.clear();
}


void Analyze::qsampler_tick()
{
    for (size_t i = 0; i < K; ++i) {
        qsampler_tick_queue.push(i);
    }

    for (size_t i = 0; i < K; ++i) {
        qsampler_tock_queue.pop();
    }
}


void Analyze::qsampler_update_hyperparameters(const std::vector<double>& params)
{
    for (size_t i = 0; i < K; ++i) {
        qsamplers[i]->hp.scale = scale[i];
        qsamplers[i]->hp.tgroup_nu = tgroup_nu;

        // Stan lays out the paramers in a flat vector. Changing the model will
        // fuck that up, so be careful.
        size_t c = condition[i];
        for (size_t j = 0; j < T; ++j) {
            qsamplers[i]->hp.tgroup_mu[j] = params[c*T + j];
            qsamplers[i]->hp.tgroup_sigma[j] = params[T*C + c*T + j];
        }
    }
}


void Analyze::compute_ts(std::vector<std::vector<double> >& ts)
{
    for (unsigned int i = 0; i < K; ++i) {
        std::fill(ts[i].begin(), ts[i].end(), 0.0);
        for (TranscriptSet::iterator t = transcripts.begin(); t != transcripts.end(); ++t) {
            ts[i][t->tgroup] += Q(i, t->id);
        }

        BOOST_FOREACH (double& x, ts[i]) {
            // TODO: scale
            x = log(x);
        }
    }
}


void Analyze::compute_xs(std::vector<std::vector<double> >& xs)
{
    for (unsigned int i = 0; i < K; ++i) {
        std::fill(tgroup_expr.begin(), tgroup_expr.end(), 0.0);
        for (TranscriptSet::iterator t = transcripts.begin(); t != transcripts.end(); ++t) {
            tgroup_expr[t->tgroup] += Q(i, t->id);
        }

        for (TranscriptSet::iterator t = transcripts.begin(); t != transcripts.end(); ++t) {
            xs[i][t->id] = Q(i, t->id) / tgroup_expr[t->tgroup];
        }
    }
}


void Analyze::run()
{
    C = condition_index.size();
    Q.resize(K, N);
    scale.resize(K, 1.0);

    // TODO: This all neeeds to be rewritten now than stan is out of the picture
#if 0
    AnalyzeSamplerData sampler_data(*this);
    model_t model(sampler_data, &std::cout);

    setup();
    BOOST_FOREACH (Sampler* qsampler, qsamplers) {
        qsampler->start();
    }

    qsampler_threads.resize(constants::num_threads);
    BOOST_FOREACH (SamplerTickThread*& thread, qsampler_threads) {
        thread = new SamplerTickThread(qsamplers, Q, qsampler_tick_queue,
                                       qsampler_tock_queue);
        thread->start();
    }

    std::vector<double> cont_params0(model.num_params_r());
    std::vector<int> disc_params0(model.num_params_i());
    choose_initial_values(cont_params0, disc_params0);

    qsampler_update_hyperparameters(cont_params0);
    qsampler_tick();

    compute_ts(model.ts);
    compute_xs(model.xs);

    double init_log_prob;
    std::vector<double> init_grad;
    init_log_prob = stan::model::log_prob_grad<true, true>(model, cont_params0, disc_params0,
                                                           init_grad, &std::cout);

    if (!boost::math::isfinite(init_log_prob)) {
        Logger::abort("Non-finite initial log-probability: %f", init_log_prob);
    }

    BOOST_FOREACH (double d, init_grad) {
        if (!boost::math::isfinite(d)) {
            Logger::abort("Initial gradient contains a non-finite value: %f", d);
        }
    }

    // TODO: pass these numbers in
    unsigned int seed = 0;
    const double epsilon = 1.0;
    const double epsilon_pm = 0.5;
    int max_treedepth = 2;
    double delta = 5.0;
    double gamma = 0.05;

    const char* task_name = "Sampling";
    Logger::push_task(task_name, burnin + num_samples);

    rng_t base_rng(seed);
    stan::mcmc::sample s(cont_params0, disc_params0, 0, 0);
    sampler_t sampler(model, base_rng, burnin);
    sampler.seed(cont_params0, disc_params0);

    // Burnin
    // ------

#if 0
    try {
        sampler.init_stepsize();
    } catch (std::runtime_error e) {
        Logger::abort("Error setting sampler step size: %s", e.what());
    }
#endif
    sampler.set_nominal_stepsize(epsilon);

    sampler.set_stepsize_jitter(epsilon_pm);
    sampler.set_max_depth(max_treedepth);
    sampler.get_stepsize_adaptation().set_delta(delta);
    sampler.get_stepsize_adaptation().set_gamma(gamma);
    sampler.get_stepsize_adaptation().set_mu(log(10 * sampler.get_nominal_stepsize()));
    sampler.engage_adaptation();

    for (size_t i = 0; i < burnin; ++i) {
        qsampler_update_hyperparameters(s.cont_params());
        qsampler_tick();

        compute_ts(model.ts);
        compute_xs(model.xs);

        const double* s_mu = &s.cont_params().at(0);
        const double* s_sigma = &s.cont_params().at(C*T);
        const double* s_alpha = &s.cont_params().at(C*T + C*T);
        const double* s_beta = &s.cont_params().at(C*T + C*T + T);

        s = sampler.transition(s);

        Logger::get_task(task_name).inc();
    }

    sampler.disengage_adaptation();


    // Samples
    // -------

    std::fstream diagnostic_stream("isolator_analyze_diagnostics.csv",
                                    std::fstream::out);
    std::fstream sample_stream("isolator_analyze_samples.csv",
                                std::fstream::out);
    stan::io::mcmc_writer<model_t> writer(&sample_stream, &diagnostic_stream);

    for (size_t i = 0; i < num_samples; ++i) {
        qsampler_update_hyperparameters(s.cont_params());
        qsampler_tick();

        compute_ts(model.ts);
        compute_xs(model.xs);
        s = sampler.transition(s);

        const double* s_mu = &s.cont_params().at(0);
        const double* s_sigma = &s.cont_params().at(C*T);
        const double* s_alpha = &s.cont_params().at(C*T + C*T);
        const double* s_beta = &s.cont_params().at(C*T + C*T + T);

        Logger::get_task(task_name).inc();
    }

    diagnostic_stream.close();
    sample_stream.close();

    Logger::pop_task(task_name);


    // Cleanup
    // -------

    // end the sampler threads
    for (size_t i = 0; i < constants::num_threads; ++i) {
        qsampler_tick_queue.push(-1);
    }

    BOOST_FOREACH (SamplerTickThread*& thread, qsampler_threads) {
        thread->join();
        delete thread;
    }

    cleanup();

#endif
}


#if 0
// Compute normalization constants for each sample.
void Analyze::compute_depth()
{
    const char* task_name = "Computing sample normalization constants";
    Logger::push_task(task_name, quantification.size() * (M/10));

    std::vector<double> sortedcol(N);
    depth.resize(K);

    // This is a posterior mean upper-quartile, normalized to the first sample
    // so depth tends to be close to 1.
    unsigned int i = 0;
    BOOST_FOREACH (matrix<float>& Q, quantification) {
        for (unsigned int j = 0; j < M; ++j) {
            matrix_column<matrix<float> > col(Q, i);
            std::copy(col.begin(), col.end(), sortedcol.begin());
            std::sort(sortedcol.begin(), sortedcol.end());
            depth[i] += gsl_stats_quantile_from_sorted_data(&sortedcol.at(0), 1, N, 0.75);
            if (j % 10 == 0) Logger::get_task(task_name).inc();
        }
        depth[i] /= M;
        ++i;
    }

    for (unsigned int i = 1; i < K; ++i) {
        depth[i] = depth[i] / depth[0];
    }
    depth[0] = 1.0;

    Logger::pop_task(task_name);
}
#endif


void Analyze::choose_initial_values(std::vector<double>& cont_params,
                                    std::vector<int>& disc_params)
{
    UNUSED(disc_params);
    assert(disc_params.empty());

    size_t off = 0;

    // tgroup_mu
    const double tgroup_mu_0 = -10;
    for (size_t j = 0; j < T; ++j) {
        for (size_t i = 0; i < C; ++i) {
            cont_params[off++] = tgroup_mu_0;
        }
    }

    // tgroup_sigma
    const double tgroup_sigma_0 = 100.0;
    //const double tgroup_sigma_0 =
        //(tgroup_alpha_alpha / tgroup_beta_alpha) / (tgroup_alpha_beta / tgroup_beta_beta);
    for (size_t j = 0; j < T; ++j) {
        for (size_t i = 0; i < C; ++i) {
            cont_params[off++] = tgroup_sigma_0;
        }
    }

    // tgroup_alpha
    //const double tgroup_alpha_0 = tgroup_alpha_alpha / tgroup_beta_alpha;
    const double tgroup_alpha_0 = tgroup_sigma_0;
    for (size_t i = 0; i < T; ++i) {
        cont_params[off++] = tgroup_alpha_0;
    }

    // tgroup_beta
    //const double tgroup_beta_0 = tgroup_alpha_beta / tgroup_beta_beta;
    const double tgroup_beta_0 = 1.0;
    for (size_t i = 0; i < T; ++i) {
        cont_params[off++] = tgroup_beta_0;
    }

    assert(off == cont_params.size());
}

