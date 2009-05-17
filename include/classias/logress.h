/*
 *		Training logistic regression models with L-BFGS.
 *
 * Copyright (c) 2008,2009 Naoaki Okazaki
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the Northwestern University, University of Tokyo,
 *       nor the names of its contributors may be used to endorse or promote
 *       products derived from this software without specific prior written
 *       permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER
 * OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* $Id$ */

#ifndef __CLASSIAS_LOGRESS_H__
#define __CLASSIAS_LOGRESS_H__

#include <float.h>
#include <cmath>
#include <ctime>
#include <set>
#include <string>
#include <vector>
#include <iostream>

#include "lbfgs.h"
#include "evaluation.h"
#include "parameters.h"

namespace classias
{

template <
    class key_tmpl,
    class value_tmpl,
    class model_tmpl
>
class linear_binary_instance
{
public:
    typedef key_tmpl key_type;
    typedef value_tmpl value_type;
    typedef model_tmpl model_type;

protected:
    model_type& m_model;
    value_type m_score;

public:
    linear_binary_instance(model_type& model)
        : m_model(model)
    {
        m_score = 0.;
    }

    virtual ~linear_binary_instance()
    {
    }

    inline operator bool() const
    {
        return (0. < m_score);
    }

    inline void operator()(const key_type& key, const value_type& value)
    {
        m_score += m_model[key] * value;        
    }

    inline value_type score() const
    {
        return m_score;
    }

    inline value_type logistic_prob() const
    {
        return (-100. < m_score ? (1. / (1. + std::exp(-m_score))) : 0.);
    }

    inline value_type logistic_error(bool b) const
    {
        double p = 0.;
        if (-100. < m_score) {
            p = 0.;
        } else if (100. < m_score) {
            p = 1.;
        } else {
            p = 1. / (1. + std::exp(-m_score));
        }
        return (static_cast<double>(b) - p);
    }

    inline value_type logistic_error(bool b, value_type& logp) const
    {
        double p = 0.;
        if (-100. < m_score) {
            p = 0.;
            logp = static_cast<double>(b) * m_score;
        } else if (100. < m_score) {
            p = 1.;
            logp = (static_cast<double>(b) - 1.) * m_score;
        } else {
            p = 1. / (1. + std::exp(-m_score));
            logp = b ? std::log(p) : std::log(1.-p);
        }
        return (static_cast<double>(b) - p);
    }
};


/**
 * Training a logistic regression model.
 */
template <
    class data_tmpl,
    class value_tmpl = double
>
class trainer_logress : public lbfgs_solver
{
public:
    /// A type representing a data set for training.
    typedef data_tmpl data_type;
    /// A type representing values for internal computations.
    typedef value_tmpl value_type;
    /// A synonym of this class.
    typedef trainer_logress<data_type, value_type> this_class;
    /// A type representing an instance in the training data.
    typedef typename data_type::instance_type instance_type;
    /// .
    typedef typename instance_type::features_type features_type;
    typedef typename features_type::identifier_type feature_type;

    /// A type providing a read-only random-access iterator for instances.
    typedef typename data_type::const_iterator const_iterator;

    /// An array [K] of feature weights.
    value_type *m_weights;

protected:
    /// A data set for training.
    const data_type* m_data;
    /// A group number for holdout evaluation.
    int m_holdout;

    /// Parameters interface.
    parameter_exchange m_params;
    /// Regularization type.
    std::string m_regularization;
    /// Regularization sigma;
    value_type m_regularization_sigma;
    /// The start index of regularization.
    int m_regularization_start;
    /// The number of memories in L-BFGS.
    int m_lbfgs_num_memories;
    /// L-BFGS epsilon for convergence.
    value_type m_lbfgs_epsilon;
    /// Number of iterations for stopping criterion.
    int m_lbfgs_stop;
    /// The delta threshold for stopping criterion.
    value_type m_lbfgs_delta;
    /// Maximum number of L-BFGS iterations.
    int m_lbfgs_maxiter;
    /// Line search algorithm.
    std::string m_lbfgs_linesearch;
    /// The maximum number of trials for the line search algorithm.
    int m_lbfgs_max_linesearch;

    /// L1-regularization constant.
    value_type m_c1;
    /// L2-regularization constant.
    value_type m_c2;

    /// An output stream to which this object outputs log messages.
    std::ostream* m_os;
    /// An internal variable (previous timestamp).
    clock_t m_clk_prev;

public:
    trainer_logress()
    {
        m_weights = NULL;
        m_regularization_start = 0;
        clear();
    }

    virtual ~trainer_logress()
    {
        clear();
    }

    void clear()
    {
        delete[] m_weights;
        m_weights = 0;

        m_data = NULL;
        m_os = 0;
        m_holdout = -1;

        // Initialize the parameters.
        m_params.init("regularization", &m_regularization, "L2",
            "Regularization method (prior):\n"
            "{'': no regularization, 'L1': L1-regularization, 'L2': L2-regularization}");
        m_params.init("regularization.sigma", &m_regularization_sigma, 5.0,
            "Regularization coefficient (sigma).");
        m_params.init("lbfgs.num_memories", &m_lbfgs_num_memories, 6,
            "The number of corrections to approximate the inverse hessian matrix.");
        m_params.init("lbfgs.epsilon", &m_lbfgs_epsilon, 1e-5,
            "Epsilon for testing the convergence of the log likelihood.");
        m_params.init("lbfgs.stop", &m_lbfgs_stop, 10,
            "The duration of iterations to test the stopping criterion.");
        m_params.init("lbfgs.delta", &m_lbfgs_delta, 1e-5,
            "The threshold for the stopping criterion; an L-BFGS iteration stops when the\n"
            "improvement of the log likelihood over the last ${lbfgs.stop} iterations is\n"
            "no greater than this threshold.");
        m_params.init("lbfgs.max_iterations", &m_lbfgs_maxiter, INT_MAX,
            "The maximum number of L-BFGS iterations.");
        m_params.init("lbfgs.linesearch", &m_lbfgs_linesearch, "MoreThuente",
            "The line search algorithm used in L-BFGS updates:\n"
            "{'MoreThuente': More and Thuente's method, 'Backtracking': backtracking}");
        m_params.init("lbfgs.max_linesearch", &m_lbfgs_max_linesearch, 20,
            "The maximum number of trials for the line search algorithm.");
    }

    parameter_exchange& params()
    {
        return m_params;
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
        value_type loss = 0;
        typename data_type::const_iterator iti;
        typename features_type::const_iterator itf;

        // Initialize the gradient of every weight as zero.
        for (int i = 0;i < n;++i) {
            g[i] = 0.;
        }

        // For each instance in the data.
        for (iti = m_data->begin();iti != m_data->end();++iti) {
            value_type z = 0.;
            value_type d = 0.;
            value_type logp = 0.;

            // Exclude instances for holdout evaluation.
            if (iti->get_group() == m_holdout) {
                continue;
            }

            linear_binary_instance<feature_type, value_type, const value_type*> inst(x);

            for (itf = iti->begin();itf != iti->end();++itf) {
                inst(itf->first, itf->second);
            }
            //iti->for_each(inst);

            d = inst.logistic_error(iti->get_truth(), logp);

            /*
            // Compute the instance score.
            z = iti->inner_product(x);

            if (z < -50.) {
                if (iti->get_truth()) {
                    d = 1.;
                    logp = +z;
                } else {
                    d = 0.;
                }
            } else if (50. < z) {
                if (iti->get_truth()) {
                    d = 0.;
                } else {
                    d = -1.;
                    logp = -z;
                }
            } else {
                double p = 1.0 / (1.0 + std::exp(-z));
                if (iti->get_truth()) {
                    d = 1.0 - p;
                    logp = std::log(p);
                } else {
                    d = -p;
                    logp = std::log(1-p);                
                }
            }
            */

            loss -= iti->get_weight() * logp;
            // Update the gradients for the weights.
            for (itf = iti->begin();itf != iti->end();++itf) {
                g[itf->first] -= itf->second * d * iti->get_weight();
            }
            //iti->add_to(g, -d * iti->get_weight());
        }

	    // L2 regularization.
	    if (m_c2 != 0.) {
            value_type norm = 0.;
            for (int i = m_regularization_start;i < n;++i) {
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

        return 0;
    }

    int train(
        const data_type& data,
        std::ostream& os,
        int holdout = -1,
        bool false_analysis = false
        )
    {
        const size_t K = data.traits.num_features();
        typename data_type::const_iterator it;

        // Initialize feature weights.
        m_weights = new double[K];
        for (size_t k = 0;k < K;++k) {
            m_weights[k] = 0.;
        }
        m_holdout = holdout;

        // Set the internal parameters.
        if (m_regularization == "L1" || m_regularization == "l1") {
            m_c1 = 1.0 / m_regularization_sigma;
            m_c2 = 0.;
            m_lbfgs_linesearch = "Backtracking";
        } else if (m_regularization == "L2" || m_regularization == "l2") {
            m_c1 = 0.;
            m_c2 = 1.0 / (m_regularization_sigma * m_regularization_sigma);
        } else {
            m_c1 = 0.;
            m_c2 = 0.;
        }

        m_regularization_start = data.get_user_feature_start();
        
        os << "Training a logistic regression model" << std::endl;
        m_params.show(os);
        os << std::endl;

        // Call the L-BFGS solver.
        m_os = &os;
        m_data = &data;
        m_clk_prev = clock();
        int ret = lbfgs_solve(
            (const int)K,
            m_weights,
            NULL,
            m_lbfgs_num_memories,
            m_lbfgs_epsilon,
            m_lbfgs_stop,
            m_lbfgs_delta,
            m_lbfgs_maxiter,
            m_lbfgs_linesearch,
            m_lbfgs_max_linesearch,
            m_c1,
            m_regularization_start
            );

        // Report the result from the L-BFGS solver.
        lbfgs_output_status(os, ret);

        if (holdout != -1 || false_analysis) {
            os << std::endl;
            os << "***** Final model *****" << std::endl;
            holdout_evaluation(false_analysis);
            os << std::endl;
        }

        return ret;
    }

    void holdout_evaluation(bool false_analysis = false)
    {
        std::ostream& os = *m_os;
        int positive_labels[] = {1};
        confusion_matrix matrix(2);

        if (false_analysis) {
            os << "=== False analysis ===" << std::endl;
        }

        // For each attribute_instance_base in the data_base.
        for (const_iterator iti = m_data->begin();iti != m_data->end();++iti) {
            // Skip instances for training.
            if (iti->get_group() != m_holdout) {
                continue;
            }

            // Compute the logit.
            value_type z = iti->inner_product(m_weights);

            // Obtain the label index of the reference and the model.
            int rl = (iti->get_truth() ? 1 : 0);
            int ml = (z <= 0. ? 0 : 1);

            if (false_analysis) {
                if (rl != ml) {
                    os << iti->get_comment() << std::endl;
                    os << (ml == 0 ? "-1" : "+1") << '\t' << z << std::endl;
                }
            }

            // Classify the instance.
            matrix(rl, ml)++;
        }

        if (false_analysis) {
            os << "===" << std::endl;
        }

        matrix.output_accuracy(os);
        matrix.output_micro(os, positive_labels, positive_labels+1);
    }
};

};

#endif/*__CLASSIAS_LOGRESS_H__*/
