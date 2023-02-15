#pragma once

#include "cpp_clustering/common/Utils.hpp"
#include "cpp_clustering/containers/LowerTriangleMatrix.hpp"
#include "cpp_clustering/heuristics/Heuristics.hpp"
#include "cpp_clustering/kmedoids/PAMUtils.hpp"
#include "cpp_clustering/math/random/Distributions.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
#include <numeric>
#include <tuple>
#include <variant>
#include <vector>

namespace cpp_clustering {

template <typename Iterator>
class FasterPAM {
    static_assert(std::is_floating_point_v<typename Iterator::value_type> ||
                      std::is_signed_v<typename Iterator::value_type>,
                  "FasterPAM allows floating point types or signed interger point types.");

  public:
    using DataType = typename Iterator::value_type;

    // {samples_first_, samples_last_, n_features_}
    using DatasetDescriptorType = std::tuple<Iterator, Iterator, std::size_t>;

    using FirstVariantType   = cpp_clustering::containers::LowerTriangleMatrixDynamic<Iterator>;
    using SecondVariantType  = cpp_clustering::containers::LowerTriangleMatrix<Iterator>;
    using StorageVariantType = std::variant<FirstVariantType, SecondVariantType>;

    FasterPAM(const DatasetDescriptorType& dataset_descriptor, const std::vector<std::size_t>& medoids_indices);

    FasterPAM(const DatasetDescriptorType&    dataset_descriptor,
              const std::vector<std::size_t>& medoids_indices,
              const DataType&                 loss);

    FasterPAM(const SecondVariantType& pairwise_distance_matrix, const std::vector<std::size_t>& medoids_indices);

    FasterPAM(const SecondVariantType&        pairwise_distance_matrix,
              const std::vector<std::size_t>& medoids_indices,
              const DataType&                 loss);

    FasterPAM(const FasterPAM&) = delete;

    DataType total_deviation() const;

    std::vector<std::size_t> step();

  private:
    struct Buffers {
        Buffers(const Iterator&                 samples_first,
                const Iterator&                 samples_last,
                std::size_t                     n_features,
                const std::vector<std::size_t>& medoids_indices);

        Buffers(const DatasetDescriptorType& dataset_descriptor, const std::vector<std::size_t>& medoids_indices);

        Buffers(const SecondVariantType& pairwise_distance_matrix, const std::vector<std::size_t>& medoids_indices);

        Buffers(const Buffers&) = delete;

        void update_losses_with_closest_medoid_removal(std::size_t n_medoids);

        std::vector<std::size_t> samples_to_nearest_medoid_indices_;
        std::vector<std::size_t> samples_to_second_nearest_medoid_indices_;
        std::vector<DataType>    samples_to_nearest_medoid_distances_;
        std::vector<DataType>    samples_to_second_nearest_medoid_distances_;
        std::vector<DataType>    losses_with_closest_medoid_removal_;
    };

    std::pair<DataType, std::size_t> find_best_swap(std::size_t medoid_candidate_index) const;

    DataType swap_buffers(std::size_t medoid_candidate_index, std::size_t best_swap_index);

    StorageVariantType       storage_variant_;
    std::size_t              n_samples_;
    std::vector<std::size_t> medoids_;
    std::unique_ptr<Buffers> buffers_ptr_;
    DataType                 loss_;
};

template <typename Iterator>
FasterPAM<Iterator>::FasterPAM(const DatasetDescriptorType&    dataset_descriptor,
                               const std::vector<std::size_t>& medoids_indices)
  : FasterPAM<Iterator>::FasterPAM(dataset_descriptor, medoids_indices, common::utils::infinity<DataType>()) {
    // compute initial loss
    loss_ = std::accumulate(buffers_ptr_->samples_to_nearest_medoid_distances_.begin(),
                            buffers_ptr_->samples_to_nearest_medoid_distances_.end(),
                            static_cast<DataType>(0));
}

template <typename Iterator>
FasterPAM<Iterator>::FasterPAM(const DatasetDescriptorType&    dataset_descriptor,
                               const std::vector<std::size_t>& medoids_indices,
                               const DataType&                 loss)
  : storage_variant_{FirstVariantType(dataset_descriptor)}
  , n_samples_{std::get<FirstVariantType>(storage_variant_).n_samples()}
  , medoids_{medoids_indices}
  , buffers_ptr_{std::make_unique<Buffers>(dataset_descriptor, medoids_)}
  , loss_{loss} {}

template <typename Iterator>
FasterPAM<Iterator>::FasterPAM(const SecondVariantType&        pairwise_distance_matrix,
                               const std::vector<std::size_t>& medoids_indices)
  : FasterPAM<Iterator>::FasterPAM(pairwise_distance_matrix, medoids_indices, common::utils::infinity<DataType>()) {
    // compute initial loss
    loss_ = std::accumulate(buffers_ptr_->samples_to_nearest_medoid_distances_.begin(),
                            buffers_ptr_->samples_to_nearest_medoid_distances_.end(),
                            static_cast<DataType>(0));
}

template <typename Iterator>
FasterPAM<Iterator>::FasterPAM(const SecondVariantType&        pairwise_distance_matrix,
                               const std::vector<std::size_t>& medoids_indices,
                               const DataType&                 loss)
  : storage_variant_{pairwise_distance_matrix}
  , n_samples_{std::get<SecondVariantType>(storage_variant_).n_samples()}
  , medoids_{medoids_indices}
  , buffers_ptr_{std::make_unique<Buffers>(pairwise_distance_matrix, medoids_)}
  , loss_{loss} {}

template <typename Iterator>
typename FasterPAM<Iterator>::DataType FasterPAM<Iterator>::total_deviation() const {
    return loss_;
}

template <typename Iterator>
std::vector<std::size_t> FasterPAM<Iterator>::step() {
    const auto& samples_to_nearest_medoid_indices = buffers_ptr_->samples_to_nearest_medoid_indices_;

    for (std::size_t medoid_candidate_index = 0; medoid_candidate_index < n_samples_; ++medoid_candidate_index) {
        // execute only if the current candidate is not already selected as a medoid
        if (medoid_candidate_index != samples_to_nearest_medoid_indices[medoid_candidate_index]) {
            // the total deviation change for the best swap candidate and its index in the dataset
            const auto [best_swap_delta_td, best_swap_index] = find_best_swap(medoid_candidate_index);

            if (best_swap_delta_td < 0) {
                // swap roles of medoid m* and non-medoid x_o
                medoids_[best_swap_index] = medoid_candidate_index;
                // update FasterPAM buffers
                loss_ = swap_buffers(medoid_candidate_index, best_swap_index);
                buffers_ptr_->update_losses_with_closest_medoid_removal(medoids_.size());
            }
        }
    }
    return medoids_;
}

template <typename Iterator>
std::pair<typename Iterator::value_type, std::size_t> FasterPAM<Iterator>::find_best_swap(
    std::size_t medoid_candidate_index) const {
    // TD set to the positive loss of removing medoid mi and assigning all of its members to the next best
    // alternative
    auto delta_td_mi = buffers_ptr_->losses_with_closest_medoid_removal_;
    // The negative loss of adding the replacement medoid candidate x_c
    // and reassigning all objects closest to this new medoid
    DataType delta_td_xc = 0;

    const auto& samples_to_nearest_medoid_indices          = buffers_ptr_->samples_to_nearest_medoid_indices_;
    const auto& samples_to_nearest_medoid_distances        = buffers_ptr_->samples_to_nearest_medoid_distances_;
    const auto& samples_to_second_nearest_medoid_distances = buffers_ptr_->samples_to_second_nearest_medoid_distances_;

    for (std::size_t other_sample_index = 0; other_sample_index < n_samples_; ++other_sample_index) {
        // candidate_to_other_distance
        const auto distance_oc =
            std::holds_alternative<FirstVariantType>(storage_variant_)
                ? std::get<FirstVariantType>(storage_variant_)(medoid_candidate_index, other_sample_index)
                : std::get<SecondVariantType>(storage_variant_)(medoid_candidate_index, other_sample_index);

        // other_sample_to_nearest_medoid_index
        const auto& index_1 = samples_to_nearest_medoid_indices[other_sample_index];
        // other_sample_to_nearest_medoid_distance
        const auto& distance_1 = samples_to_nearest_medoid_distances[other_sample_index];
        // other_sample_to_second_nearest_medoid_distance
        const auto& distance_2 = samples_to_second_nearest_medoid_distances[other_sample_index];

        if (distance_oc < distance_1) {
            delta_td_xc += distance_oc - distance_1;
            // ∆TD{+nearest(o)} ← ∆TD{+nearest(o)} + d{nearest(o)} − d{second(o)};
            delta_td_mi[index_1] += distance_1 - distance_2;

        } else if (distance_oc < distance_2) {
            // ∆TD{+nearest(o)} ← ∆TD{+nearest(o)} + d_oj − d{second(o)};
            delta_td_mi[index_1] += distance_oc - distance_2;
        }
    }
    // i ← argmin(∆TD_i), with i: index of medoids elements
    const auto [best_swap_index, best_swap_distance] =
        common::utils::get_min_index_value_pair(delta_td_mi.begin(), delta_td_mi.end());

    return {delta_td_xc + best_swap_distance, best_swap_index};
}

template <typename Iterator>
typename Iterator::value_type FasterPAM<Iterator>::swap_buffers(std::size_t medoid_candidate_index,
                                                                std::size_t best_swap_index) {
    DataType loss = 0;

    auto& samples_to_nearest_medoid_indices          = buffers_ptr_->samples_to_nearest_medoid_indices_;
    auto& samples_to_second_nearest_medoid_indices   = buffers_ptr_->samples_to_second_nearest_medoid_indices_;
    auto& samples_to_nearest_medoid_distances        = buffers_ptr_->samples_to_nearest_medoid_distances_;
    auto& samples_to_second_nearest_medoid_distances = buffers_ptr_->samples_to_second_nearest_medoid_distances_;

    for (std::size_t other_sample_index = 0; other_sample_index < n_samples_; ++other_sample_index) {
        // other_sample_to_nearest_medoid_index
        auto& index_1 = samples_to_nearest_medoid_indices[other_sample_index];
        // other_sample_to_seacond_nearest_medoid_index
        auto& index_2 = samples_to_second_nearest_medoid_indices[other_sample_index];
        // other_sample_to_nearest_medoid_distance
        auto& distance_1 = samples_to_nearest_medoid_distances[other_sample_index];
        // other_sample_to_second_nearest_medoid_distance
        auto& distance_2 = samples_to_second_nearest_medoid_distances[other_sample_index];

        if (other_sample_index == medoid_candidate_index) {
            if (index_1 != best_swap_index) {
                index_2    = index_1;  // if we keep a record of second nearest indices
                distance_2 = distance_1;
            }
            index_1    = best_swap_index;
            distance_1 = 0;
            continue;
        }
        // candidate_to_other_distance
        const auto distance_oc =
            std::holds_alternative<FirstVariantType>(storage_variant_)
                ? std::get<FirstVariantType>(storage_variant_)(medoid_candidate_index, other_sample_index)
                : std::get<SecondVariantType>(storage_variant_)(medoid_candidate_index, other_sample_index);

        // nearest medoid is gone
        if (index_1 == best_swap_index) {
            if (distance_oc < distance_2) {
                index_1    = best_swap_index;
                distance_1 = distance_oc;

            } else {
                index_1    = index_2;
                distance_1 = distance_2;
                // START: update second nearest medoid
                std::size_t index_tmp    = best_swap_index;
                auto        distance_tmp = distance_oc;
                for (std::size_t idx = 0; idx < medoids_.size(); ++idx) {
                    if (idx != index_1 && idx != best_swap_index) {
                        // distance from other object to looped medoid
                        const auto distance_om =
                            std::holds_alternative<FirstVariantType>(storage_variant_)
                                ? std::get<FirstVariantType>(storage_variant_)(medoids_[idx], other_sample_index)
                                : std::get<SecondVariantType>(storage_variant_)(medoids_[idx], other_sample_index);

                        if (distance_om < distance_tmp) {
                            index_tmp    = idx;
                            distance_tmp = distance_om;
                        }
                    }
                }
                index_2    = index_tmp;
                distance_2 = distance_tmp;
                // END: update second nearest medoid
            }
        } else {
            // nearest not removed
            if (distance_oc < distance_1) {
                index_2    = index_1;
                distance_2 = distance_1;
                index_1    = best_swap_index;
                distance_1 = distance_oc;

            } else if (distance_oc < distance_2) {
                index_2    = best_swap_index;
                distance_2 = distance_oc;

            } else if (index_2 == best_swap_index) {
                // second nearest was replaced
                // START: update second nearest medoid
                std::size_t index_tmp    = best_swap_index;
                auto        distance_tmp = distance_oc;
                for (std::size_t idx = 0; idx < medoids_.size(); ++idx) {
                    if (idx != index_1 && idx != best_swap_index) {
                        // distance from other object to looped medoid
                        const auto distance_om =
                            std::holds_alternative<FirstVariantType>(storage_variant_)
                                ? std::get<FirstVariantType>(storage_variant_)(medoids_[idx], other_sample_index)
                                : std::get<SecondVariantType>(storage_variant_)(medoids_[idx], other_sample_index);

                        if (distance_om < distance_tmp) {
                            index_tmp    = idx;
                            distance_tmp = distance_om;
                        }
                    }
                }
                index_2    = index_tmp;
                distance_2 = distance_tmp;
                // END: update second nearest medoid
            }
        }
        loss += distance_1;
    }
    return loss;
}

template <typename Iterator>
FasterPAM<Iterator>::Buffers::Buffers(const Iterator&                 samples_first,
                                      const Iterator&                 samples_last,
                                      std::size_t                     n_features,
                                      const std::vector<std::size_t>& medoids_indices)
  : samples_to_nearest_medoid_indices_{pam::utils::samples_to_nth_nearest_medoid_indices(samples_first,
                                                                                         samples_last,
                                                                                         n_features,
                                                                                         medoids_indices,
                                                                                         /*n_closest=*/1)}
  , samples_to_second_nearest_medoid_indices_{pam::utils::samples_to_nth_nearest_medoid_indices(samples_first,
                                                                                                samples_last,
                                                                                                n_features,
                                                                                                medoids_indices,
                                                                                                /*n_closest=*/2)}
  , samples_to_nearest_medoid_distances_{pam::utils::samples_to_nth_nearest_medoid_distances(samples_first,
                                                                                             samples_last,
                                                                                             n_features,
                                                                                             medoids_indices,
                                                                                             /*n_closest=*/1)}
  , samples_to_second_nearest_medoid_distances_{pam::utils::samples_to_nth_nearest_medoid_distances(samples_first,
                                                                                                    samples_last,
                                                                                                    n_features,
                                                                                                    medoids_indices,
                                                                                                    /*n_closest=*/2)}
  , losses_with_closest_medoid_removal_{
        pam::utils::compute_losses_with_closest_medoid_removal<DataType>(samples_to_nearest_medoid_indices_,
                                                                         samples_to_nearest_medoid_distances_,
                                                                         samples_to_second_nearest_medoid_distances_,
                                                                         medoids_indices.size())} {}

template <typename Iterator>
FasterPAM<Iterator>::Buffers::Buffers(const DatasetDescriptorType&    dataset_descriptor,
                                      const std::vector<std::size_t>& medoids_indices)
  : FasterPAM<Iterator>::Buffers::Buffers(std::get<0>(dataset_descriptor),
                                          std::get<1>(dataset_descriptor),
                                          std::get<2>(dataset_descriptor),
                                          medoids_indices) {}

template <typename Iterator>
FasterPAM<Iterator>::Buffers::Buffers(const SecondVariantType&        pairwise_distance_matrix,
                                      const std::vector<std::size_t>& medoids_indices)
  : samples_to_nearest_medoid_indices_{pam::utils::samples_to_nth_nearest_medoid_indices(pairwise_distance_matrix,
                                                                                         medoids_indices,
                                                                                         /*n_closest=*/1)}
  , samples_to_second_nearest_medoid_indices_{pam::utils::samples_to_nth_nearest_medoid_indices(
        pairwise_distance_matrix,
        medoids_indices,
        /*n_closest=*/2)}
  , samples_to_nearest_medoid_distances_{pam::utils::samples_to_nth_nearest_medoid_distances(pairwise_distance_matrix,
                                                                                             medoids_indices,
                                                                                             /*n_closest=*/1)}
  , samples_to_second_nearest_medoid_distances_{pam::utils::samples_to_nth_nearest_medoid_distances(
        pairwise_distance_matrix,
        medoids_indices,
        /*n_closest=*/2)}
  , losses_with_closest_medoid_removal_{
        pam::utils::compute_losses_with_closest_medoid_removal<DataType>(samples_to_nearest_medoid_indices_,
                                                                         samples_to_nearest_medoid_distances_,
                                                                         samples_to_second_nearest_medoid_distances_,
                                                                         medoids_indices.size())} {}

template <typename Iterator>
void FasterPAM<Iterator>::Buffers::update_losses_with_closest_medoid_removal(std::size_t n_medoids) {
    losses_with_closest_medoid_removal_ =
        pam::utils::compute_losses_with_closest_medoid_removal<DataType>(samples_to_nearest_medoid_indices_,
                                                                         samples_to_nearest_medoid_distances_,
                                                                         samples_to_second_nearest_medoid_distances_,
                                                                         n_medoids);
}

}  // namespace cpp_clustering