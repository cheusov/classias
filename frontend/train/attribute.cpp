/*
 *		Data I/O for attribute-based classification.
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
<line>          ::= <comment> | <instance> | <br>
<comment>       ::= "#" <string> <br>
<instance>      ::= <class> ("\t" <attribute>)+ <br>
<class>         ::= <string>
<attribute>     ::= <name> [ ":" <weight> ]
<name>          ::= <string>
<weight>        ::= <numeric>
<br>            ::= "\n"
*/

template <
    class instance_type,
    class attributes_quark_type,
    class label_quark_type
>
static void
read_line(
    const std::string& line,
    instance_type& instance,
    attributes_quark_type& attributes,
    label_quark_type& labels,
    const option& opt,
    int lines = 0
    )
{
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

    // Set the instance label.
    instance.set_label(labels(*itv));

    // Set attributes for the instance.
    for (++itv;itv != values.end();++itv) {
        if (!itv->empty()) {
            double value;
            std::string name;
            get_name_value(*itv, name, value);
            instance.attributes.append(attributes(name), value);
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
    typedef typename data_type::iterator iterator;

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

        // A new candidate.
        read_line(line, data.new_element(), data.features, data.labels, opt, lines);
    }

    /*
    // Set the end index of the user features.
    data.set_user_feature_end(data.features.size());

    // Generate a bias feature if necessary.
    if (opt.generate_bias) {
        // Allocate a bias feature.
        feature_type bf = data.features("@bias");

        // Insert the bias feature to each instance.
        for (iterator it = data.begin();it != data.end();++it) {
            it->append(bf, 1.0);
        }
    }
    */
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
    typedef typename data_type::label_quark_type labels_quark_type;
    typedef typename data_type::traits_type traits_type;
    typedef typename traits_type::attribute_type attribute_type;
    typedef typename traits_type::label_type label_type;
    typedef typename features_quark_type::value_type features_type;
    const features_quark_type& features = data.features;

    // Open a model file for writing.
    std::ofstream os(opt.model.c_str());

    // Output a model type.
    os << "@model" << '\t' << "attribute-label" << std::endl;

    // Output a set of labels.
    os << "@labels";
    for (labels_quark_type::value_type l = 0;l < data.labels.size();++l) {
        os << '\t' << data.labels.to_item(l);
    }
    os << std::endl;

    // Store the feature weights.
    for (features_type i = 0;i < features.size();++i) {
        value_type w = weights[i];
        if (w != 0.) {
            attribute_type a;
            label_type l;
            data.traits.backward(i, a, l);
            os << w << '\t'
                << data.features.to_item(a) << '\t'
                << data.labels.to_item(l) << std::endl;
        }
    }
}

int attribute_train(option& opt)
{
    // Branches for training algorithms.
    if (opt.algorithm == "maxent") {
        if (opt.type == option::TYPE_ATTRIBUTE_DENSE) {
            return train<
                classias::ddata,
                classias::trainer_maxent<classias::ddata, double>
            >(opt);
        } else {
            return train<
                classias::adata,
                classias::trainer_maxent<classias::adata, double>
            >(opt);
        }
    } else {
        throw invalid_algorithm(opt.algorithm);
    }
}

bool attribute_usage(option& opt)
{
    if (opt.algorithm == "maxent") {
        classias::trainer_maxent<classias::adata, double> tr;
        tr.params().help(opt.os);
        return true;
    }
    return false;
}
