/*
 *		Data I/O for multi-candidate classification.
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

#ifdef  HAVE_CONFIG_H
#include <config.h>
#endif/*HAVE_CONFIG_H*/

#include <iostream>
#include <string>

#include <classias/classias.h>
#include <classias/maxent.h>

#include "option.h"
#include "tokenize.h"
#include "train.h"

/*
<line>          ::= <comment> | <boi> | <eoi> | <candidate> | <br>
<comment>       ::= "#" <string> <br>
<boi>           ::= "@boi" <br>
<eoi>           ::= "@eoi" <br>
<instance>      ::= <class> [ <label> ] ("\t" <feature>)+ <br>
<class>         ::= "F" | "T"
<label>         ::= <name>
<feature>       ::= <name> [ ":" <weight> ]
<name>          ::= <string>
<weight>        ::= <numeric>
<br>            ::= "\n"
*/

template <
    class instance_type,
    class features_quark_type,
    class label_quark_type
>
static void
read_line(
    const std::string& line,
    instance_type& instance,
    features_quark_type& features,
    label_quark_type& labels,
    const option& opt,
    int lines = 0
    )
{
    typedef typename instance_type::candidate_type candidate_type;

    // Split the line with tab characters.
    tokenizer values(line, '\t');
    tokenizer::iterator itv = values.begin();
    if (itv == values.end()) {
        throw invalid_data("no field found in the line", lines);
    }

    // Make sure that the first token (class) is not empty.
    if (itv->empty()) {
        throw invalid_data("an empty label found", lines);
    }

    // Set the truth value for this candidate.
    bool truth = false;
    if (itv->compare(0, 1, "T") == 0) {
        truth = true;
    } else if (itv->compare(0, 1, "F") == 0) {
        truth = false;
    } else {
        throw invalid_data("a class label must begins with either 'T' or 'F'", lines);
    }

    // Obtain the label.
    std::string label(*itv);

    // Create a new candidate.
    candidate_type& cand = instance.new_element();
    cand.set_truth(truth);
    cand.set_label(labels(label));

    // Set featuress for the instance.
    for (++itv;itv != values.end();++itv) {
        if (!itv->empty()) {
            double value;
            std::string name;
            get_name_value(*itv, name, value);
            cand.append(features(name), value);
        }
    }
}

template <
    class data_type
>
static void
read_stream(
    std::istream& is,
    data_type& data,
    const option& opt,
    int group = 0
    )
{
    int lines = 0;
    typedef typename data_type::instance_type instance_type;
    typedef typename data_type::feature_type feature_type;
    typedef typename data_type::iterator data_iterator;
    typedef typename instance_type::iterator instance_iterator;

    for (;;) {
        // Read a line.
        std::string line;
        std::getline(is, line);
        if (is.eof()) {
            break;
        }
        ++lines;

        // Skip an empty line.
        if (line.empty()) {
            continue;
        }

        // Skip a comment line.
        if (line.compare(0, 1, "#") == 0) {
            continue;
        }

        // Read features that should not be regularized.
        if (line.compare(0, 14, "@unregularize\t") == 0) {
            if (0 < data.features.size()) {
                throw invalid_data("Declarative @unregularize must precede an instance", lines);
            }

            // Feature names separated by TAB characters.
            tokenizer values(line, '\t');
            tokenizer::iterator itv = values.begin();
            for (++itv;itv != values.end();++itv) {
                // Reserve early feature identifiers.
                data.features(*itv);
            }

            // Set the start index of the user features.
            data.set_user_feature_start(data.features.size());

        } else if (line.compare(0, 4, "@boi") == 0) {
            // Start of a new instance.
            instance_type& inst = data.new_element();
            inst.set_group(group);

        } else if (line.compare(0, 4, "@eoi") == 0) {

        } else if (line.compare(0, 9, "@negative") == 0) {
            // 
        } else {
            // A new candidate.
            read_line(line, data.back(), data.features, data.labels, opt, lines);
        }
    }

    data.append_positive_label(data.labels("TP"));

    // Generate a bias feature if necessary.
    if (opt.generate_bias) {
        // Insert the bias feature to each instance.
        for (data_iterator iti = data.begin();iti != data.end();++iti) {
            for (instance_iterator itc = iti->begin();itc != iti->end();++itc) {
                // A bias feature for the candidate label.
                std::string name = "@bias@" + data.labels.to_item(itc->get_label());
                itc->append(data.features(name), 1.0);
            }
        }
    }
}

template <
    class data_type,
    class value_type
>
static void
output_model(
    data_type& data,
    const value_type* weights,
    const option& opt
    )
{
    typedef typename data_type::features_quark_type features_quark_type;
    typedef typename features_quark_type::value_type features_type;
    const features_quark_type& features = data.features;

    // Open a model file for writing.
    std::ofstream os(opt.model.c_str());

    // Output a model type.
    os << "@model" << '\t' << "multi" << std::endl;

    // Store the feature weights.
    for (features_type i = 0;i < features.size();++i) {
        value_type w = weights[i];
        if (w != 0.) {
            os << w << '\t' << features.to_item(i) << std::endl;
        }
    }
}

int multi_train(option& opt)
{
    // Branches for training algorithms.
    if (opt.algorithm == "maxent") {
        return train<
            classias::mdata,
            classias::trainer_maxent<classias::mdata, double>
        >(opt);
    } else {
        throw invalid_algorithm(opt.algorithm);
    }
}

bool multi_usage(option& opt)
{
    // Branches for training algorithms.
    if (opt.algorithm == "maxent") {
        classias::trainer_maxent<classias::mdata, double> tr;
        tr.params().help(opt.os);
        return true;
    }
    return false;
}
