#ifndef __CLASSIAS_MAXENT_H__
#define __CLASSIAS_MAXENT_H__

#include <float.h>
#include <cmath>
#include <ctime>
#include <iostream>

#include "lbfgs.h"

namespace classias
{

/**
 * Training a log-linear model using the maximum entropy modeling.
 *  @param  data_tmpl           Training data class.
 *  @param  value_tmpl          The type for computation.
 */
template <
    class data_tmpl,
    class value_tmpl = double
>
class trainer_maxent : public lbfgs_solver
{
protected:
    /// A type representing a data set for training.
    typedef data_tmpl data_type;
    /// A type representing values for internal computations.
    typedef value_tmpl value_type;
    /// A synonym of this class.
    typedef trainer_maxent<data_type, value_type> this_class;
    /// A type representing an instance in the training data.
    typedef typename data_type::instance_type instance_type;
    /// A type representing a candidate for an instance.
    typedef typename instance_type::candidate_type candidate_type;
    /// A type providing a read-only random-access iterator for instances.
    typedef typename data_type::const_iterator const_iterator;

    /// An array [K] of observation expectations.
    value_type *m_oexps;
    /// An array [K] of model expectations.
    value_type *m_mexps;
    /// An array [K] of feature weights.
    value_type *m_weights;
    /// An array [M] of scores for candidate labels.
    value_type *m_scores;

    /// A group number used for holdout evaluation.
    int m_holdout;
    /// Maximum number of iterations.
    int m_maxiter;
    /// Epsilon.
    value_type m_epsilon;
    /// L1-regularization constant.
    value_type m_c1;
    /// L2-regularization constant.
    value_type m_c2;

    /// A data set for training.
    const data_type* m_data;
    /// An output stream to which this object outputs log messages.
    std::ostream* m_os;
    /// An internal variable (previous timestamp).
    clock_t m_clk_prev;

public:
    trainer_maxent()
    {
        m_oexps = 0;
        m_mexps = 0;
        m_weights = 0;
        m_scores = 0;

        m_holdout = -1;
        m_maxiter = 1000;
        m_epsilon = 1e-5;
        m_c1 = 0;
        m_c2 = 0;

        clear();
    }

    virtual ~trainer_maxent()
    {
        clear();
    }

    void clear()
    {
        delete[] m_weights;
        delete[] m_mexps;
        delete[] m_oexps;
        delete[] m_scores;
        m_oexps = 0;
        m_mexps = 0;
        m_weights = 0;
        m_scores = 0;

        m_data = NULL;
        m_os = NULL;
    }

    static bool parse_param(const std::string& param, const std::string& name, std::string& value)
    {
        if (param.compare(0, name.length(), name) == 0) {
            value = param.substr(name.length());
            return true;
        } else {
            return false;
        }
    }

    bool set(const std::string& param)
    {
        std::string str;

        if (parse_param(param, "regularization.l1=", str)) {
            double d = std::atoi(str.c_str());
            m_c1 = (d <= 0.) ? 0. : 1.0 / d;
            return true;
        } else if (parse_param(param, "regularization.l2=", str)) {
            double d = std::atoi(str.c_str());
            m_c2 = (d <= 0.) ? 0. : 1.0 / d;
            return true;
        } else if (parse_param(param, "lbfgs.maxiter=", str)) {
            int i = std::atoi(str.c_str());
            m_maxiter = i;
            return true;
        } else if (parse_param(param, "lbfgs.epsilon=", str)) {
            m_epsilon = std::atoi(str.c_str());
            return true;
        }
    }

    const value_type* get_weights() const
    {
        return m_weights;
    }

    virtual value_type lbfgs_evaluate(
        const value_type *x,
        value_type *g,
        const int n,
        const value_type step
        )
    {
        int i;
        value_type loss = 0, norm = 0;
        const data_type& data = *m_data;

        // Initialize the model expectations as zero.
        for (i = 0;i < n;++i) {
            m_mexps[i] = 0.;
        }

        // For each instance in the data.
        for (const_iterator iti = data.begin();iti != data.end();++iti) {
            value_type logp = 0.;
            value_type norm = 0.;
            typename instance_type::const_iterator itc;

            // Exclude instances for holdout evaluation.
            if (iti->get_group() == m_holdout) {
                continue;
            }

            // Compute score[i] for each candidate #i.
            for (i = 0, itc = iti->begin();itc != iti->end();++i, ++itc) {
                m_scores[i] = itc->inner_product(x);
                if (itc->is_true()) logp = m_scores[i];
                norm = logsumexp(norm, m_scores[i], (i == 0));
            }

            // Accumulate the model expectations of attributes.
            for (i = 0, itc = iti->begin();itc != iti->end();++i, ++itc) {
                itc->add(m_mexps, std::exp(m_scores[i] - norm));
            }

            // Accumulate the loss for predicting the instance.
            loss -= (logp - norm);
        }

        // Compute the gradients.
        for (int i = 0;i < n;++i) {
            g[i] = -(m_oexps[i] - m_mexps[i]);
        }

        // Apply L2 regularization if necessary.
        if (m_c2 != 0.) {
            value_type norm = 0.;
            for (int i = 0;i < n;++i) {
                g[i] += (m_c2 * x[i]);
                norm += x[i] * x[i];
            }
            loss += (m_c2 * norm * 0.5);
        }

        return loss;
    }

    virtual int lbfgs_progress(
        const value_type *x,
        const value_type *g,
        const value_type fx,
        const value_type xnorm,
        const value_type gnorm,
        const value_type step,
        int n,
        int k,
        int ls)
    {
        // Compute the duration required for this iteration.
        std::ostream& os = *m_os;
        clock_t duration, clk = std::clock();
        duration = clk - m_clk_prev;
        m_clk_prev = clk;

        // Count the number of active features.
        int num_active = 0;
        for (int i = 0;i < n;++i) {
            if (x[i] != 0.) {
                ++num_active;
            }
        }

        // Output the current progress.
        os << "***** Iteration #" << k << " *****" << std::endl;
        os << "Log-likelihood: " << -fx << std::endl;
        os << "Feature norm: " << xnorm << std::endl;
        os << "Error norm: " << gnorm << std::endl;
        os << "Active features: " << num_active << " / " << n << std::endl;
        os << "Line search trials: " << ls << std::endl;
        os << "Line search step: " << step << std::endl;
        os << "Seconds required for this iteration: " <<
            duration / (double)CLOCKS_PER_SEC << std::endl;
        os.flush();

        // Holdout evaluation if necessary.
        if (m_holdout != -1) {
            holdout_evaluation();
        }

        // Output an empty line.
        os << std::endl;
        os.flush();

        // Check for the maximum number of iterations.
        if (m_maxiter < k) {
            return 1;
        }

        return 0;
    }

    int train(
        const data_type& data,
        std::ostream& os,
        int holdout = -1
        )
    {
        size_t M = 0;
        const size_t K = data.num_features();

        // Initialize feature expectations and weights.
        m_oexps = new double[K];
        m_mexps = new double[K];
        m_weights = new double[K];
        for (size_t k = 0;k < K;++k) {
            m_oexps[k] = 0.;
            m_mexps[k] = 0.;
            m_weights[k] = 0.;
        }
        m_holdout = holdout;

        // Report the training parameters.
        os << "Training a maximum entropy model" << std::endl;
        if (m_c1 != 0.) {
            os << "L1 regularization: " << m_c1 << std::endl;
        }
        if (m_c2 != 0.) {
            os << "L2 regularization: " << m_c2 << std::endl;
        }
        if (0 <= m_holdout) {
            os << "Holdout group: " << (m_holdout+1) << std::endl;
        }
        os << std::endl;

        // Compute observation expectations of the features.
        for (const_iterator iti = data.begin();iti != data.end();++iti) {
            // Skip instances for holdout evaluation.
            if (iti->get_group() == m_holdout) {
                continue;
            }

            // Compute the observation expectations.
            typename instance_type::const_iterator itc;
            for (itc = iti->begin();itc != iti->end();++itc) {
                if (itc->is_true()) {
                    // m_oexps[k] += 1.0 * (*itc)[k].
                    itc->add(m_oexps, 1.0);
                }
            }

            // Store the maximum number of candidates.
            if (M < iti->size()) {
                M = iti->size();
            }
        }

        // Initialze the variables used by callback functions.
        m_os = &os;
        m_data = &data;
        m_clk_prev = clock();
        m_scores = new double[M];

        // Call the L-BFGS solver.
        int ret = lbfgs_solve((const int)K, m_weights, NULL, m_epsilon, m_c1);

        // Report the result from the L-BFGS solver.
        if (ret == 0) {
            os << "L-BFGS resulted in convergence" << std::endl;
        } else {
            os << "L-BFGS terminated with error code (" << ret << ")" << std::endl;
        }

        return ret;
    }

    void holdout_evaluation()
    {
        std::ostream& os = *m_os;
        const data_type& data = *m_data;
        int num_correct = 0, num_total = 0;

        // Loop over instances.
        for (const_iterator iti = data.begin();iti != data.end();++iti) {
            // Exclude instances for holdout evaluation.
            if (iti->get_group() != m_holdout) {
                continue;
            }

            // Compute the score for each candidate #i.
            value_type score_max = -DBL_MAX;
            typename instance_type::const_iterator itc;
            typename instance_type::const_iterator itc_max = iti->end();
            for (itc = iti->begin();itc != iti->end();++itc) {
                value_type score = itc->inner_product(m_weights);

                // Store the candidate that yields the maximum score.
                if (score_max < score) {
                    score_max = score;
                    itc_max = itc;
                }
            }

            // Update the 2x2 confusion matrix.
            if (itc_max->is_true()) {
                ++num_correct;
            }
            ++num_total;
        }

        // Report accuracy, precision, recall, and f1 score.
        double accuracy = 0.;
        if (0 < num_total) {
            accuracy = num_correct / (double)num_total;
        }
        os << "Accuracy: " << accuracy << " (" << num_correct << "/" << num_total << ")" << std::endl;
    }

protected:
    static value_type logsumexp(value_type x, value_type y, int flag)
    {
        value_type vmin, vmax;

        if (flag) return y;
        if (x == y) return x + 0.69314718055;   /* log(2) */
        if (x < y) {
            vmin = x; vmax = y;
        } else {
            vmin = y; vmax = x;
        }
        if (vmin + 50 < vmax)
            return vmax;
        else
            return vmax + std::log(std::exp(vmin - vmax) + 1.0);
    }
};

};

#endif/*__CLASSIAS_MAXENT_H__*/
