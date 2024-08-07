#ifndef OPENSIM_CASOCTRANSCRIPTION_H
#define OPENSIM_CASOCTRANSCRIPTION_H
/* -------------------------------------------------------------------------- *
 * OpenSim: CasOCTranscription.h                                              *
 * -------------------------------------------------------------------------- *
 * Copyright (c) 2018 Stanford University and the Authors                     *
 *                                                                            *
 * Author(s): Christopher Dembia                                              *
 *                                                                            *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may    *
 * not use this file except in compliance with the License. You may obtain a  *
 * copy of the License at http://www.apache.org/licenses/LICENSE-2.0          *
 *                                                                            *
 * Unless required by applicable law or agreed to in writing, software        *
 * distributed under the License is distributed on an "AS IS" BASIS,          *
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.   *
 * See the License for the specific language governing permissions and        *
 * limitations under the License.                                             *
 * -------------------------------------------------------------------------- */

#include "CasOCSolver.h"

namespace CasOC {

/// This is the base class for transcription schemes that convert a
/// CasOC::Problem into a general nonlinear programming problem. If you are
/// creating a new derived class, make sure to override all virtual functions
/// and obey the settings that the user specified in the CasOC::Solver.
class Transcription {
public:
    Transcription(const Solver& solver, const Problem& problem)
            : m_solver(solver), m_problem(problem) {}
    virtual ~Transcription() = default;
    Iterate createInitialGuessFromBounds() const;
    /// Use the provided random number generator to generate an iterate.
    /// Random::Uniform is used if a generator is not provided. The generator
    /// should produce numbers with [-1, 1].
    Iterate createRandomIterateWithinBounds(
            const SimTK::Random* = nullptr) const;
    template <typename T>
    T createTimes(const T& initial_time, const T& final_time) const {
        return (final_time(0) - initial_time(0)) * m_grid + initial_time(0);
    }
    casadi::DM createQuadratureCoefficients() const {
        return createQuadratureCoefficientsImpl();
    }
    casadi::DM createMeshIndices() const {
        casadi::DM meshIndices = createMeshIndicesImpl();
        const auto shape = meshIndices.size();
        OPENSIM_THROW_IF(shape.first != 1 || shape.second != m_numGridPoints,
                OpenSim::Exception,
                "createMeshIndicesImpl() must return a row vector of shape "
                "length [1, {}], but a matrix of shape [{}, {}] was returned.",
                m_numGridPoints, shape.first, shape.second);
        OPENSIM_THROW_IF(!SimTK::isNumericallyEqual(
                                 casadi::DM::sum2(meshIndices).scalar(),
                                 m_numMeshPoints),
                OpenSim::Exception,
                "Internal error: sum of mesh indices should be the number of "
                "mesh points.");

        return meshIndices;
    }

    Solution solve(const Iterate& guessOrig);

protected:
    /// This must be called in the constructor of derived classes so that
    /// overridden virtual methods are accessible to the base class. This
    /// implementation allows initialization to occur during construction,
    /// avoiding an extra call on the instantiated object.
    /// TODO control points
    void createVariablesAndSetBounds(const casadi::DM& grid,
            int numDefectsPerMeshInterval,
            int numPointsPerMeshInterval,
            const casadi::DM& pointsForInterpControls = casadi::DM());
            // const std::vector<bool>& controlPoints);

    /// We assume all functions depend on time and parameters.
    /// "inputs" is prepended by time and postpended (?) by parameters.
    casadi::MXVector evalOnTrajectory(const casadi::Function& pointFunction,
            const std::vector<Var>& inputs,
            const casadi::Matrix<casadi_int>& timeIndices) const;

    template <typename TRow, typename TColumn>
    void setVariableBounds(Var var, const TRow& rowIndices,
            const TColumn& columnIndices, const Bounds& bounds) {
        if (bounds.isSet()) {
            const auto& lower = bounds.lower;
            m_lowerBounds[var](rowIndices, columnIndices) = lower;
            const auto& upper = bounds.upper;
            m_upperBounds[var](rowIndices, columnIndices) = upper;
        } else {
            const auto inf = std::numeric_limits<double>::infinity();
            m_lowerBounds[var](rowIndices, columnIndices) = -inf;
            m_upperBounds[var](rowIndices, columnIndices) = inf;
        }
    }

    template <typename TRow, typename TColumn>
    void setVariableScaling(Var key, const TRow& rowIndices,
        const TColumn& columnIndices, const Bounds& bounds) {
        if (m_solver.getScaleVariablesUsingBounds()) {
            const auto& lower = bounds.lower;
            const auto& upper = bounds.upper;
            double dilate = upper - lower;
            double shift;
            if (std::isinf(dilate) || std::isnan(dilate)) {
                dilate = 1;
                shift = 0;
            } else if (dilate == 0) {
                dilate = 1;
                shift = upper;
            } else {
                shift = -0.5 * (upper + lower);
            }
            m_scale.at(key)(rowIndices, columnIndices) = dilate;
            m_shift.at(key)(rowIndices, columnIndices) = shift;
        } else {
            m_scale.at(key)(rowIndices, columnIndices) = 1;
            m_shift.at(key)(rowIndices, columnIndices) = 0;
        }
    }

    template <typename T>
    struct Constraints {
        T initial_time;
        T final_time;
        T parameters;
        T defects;
        T multibody_residuals;
        T auxiliary_residuals;
        T kinematic;
        std::vector<T> endpoint;
        std::vector<T> path;
        T interp_controls;
    };
    void printConstraintValues(const Iterate& it,
            const Constraints<casadi::DM>& constraints,
            std::ostream& stream = std::cout) const;
    void printObjectiveBreakdown(const Iterate& it,
            const casadi::DM& objectiveTerms,
            std::ostream& stream = std::cout) const;

    const Solver& m_solver;
    const Problem& m_problem;
    int m_numGridPoints = -1;
    int m_numMeshPoints = -1;
    int m_numMeshIntervals = -1;
    int m_numMeshInteriorPoints = -1;
    int m_numDefectsPerMeshInterval = -1;
    int m_numPointsPerMeshInterval = -1;
    int m_numMultibodyResiduals = -1;
    int m_numAuxiliaryResiduals = -1;
    int m_numParameterConstraints = -1;
    int m_numConstraints = -1;
    int m_numPathConstraintPoints = -1;
    casadi::DM m_grid;
    casadi::DM m_pointsForInterpControls;
    casadi::MX m_times;
    casadi::MX m_duration;
    // std::vector<bool> m_controlPoints;

private:
    VariablesMXVector m_scaledVectorVars;
    VariablesMX m_scaledVars;
    VariablesMX m_unscaledVars;
    VariablesDM m_lowerBounds;
    VariablesDM m_upperBounds;
    VariablesDM m_shift;
    VariablesDM m_scale;

    casadi::DM m_meshIndicesMap;
    casadi::Matrix<casadi_int> m_gridIndices;
    casadi::Matrix<casadi_int> m_meshIndices;
    casadi::Matrix<casadi_int> m_meshInteriorIndices;
    casadi::Matrix<casadi_int> m_pathConstraintIndices;

    casadi::MX m_xdot; // State derivatives.

    casadi::MX m_objectiveTerms;
    std::vector<std::string> m_objectiveTermNames;

    Constraints<casadi::MX> m_constraints;
    Constraints<casadi::DM> m_constraintsLowerBounds;
    Constraints<casadi::DM> m_constraintsUpperBounds;

private:
    /// Override this function in your derived class to compute a vector of
    /// quadrature coeffecients (of length m_numGridPoints) required to set the
    /// the integral cost within transcribe().
    virtual casadi::DM createQuadratureCoefficientsImpl() const = 0;
    /// Override this function to specify the indicies in the grid where the
    /// mesh (or "knot") points lie.
    /// @note The returned vector must be a row vector of length m_numGridPoints
    /// with nonzero values at the mesh indices.
    virtual casadi::DM createMeshIndicesImpl() const = 0;
    /// Override this function in your derived class set the defect, kinematic,
    /// and path constraint errors required for your transcription scheme.
    virtual void calcDefectsImpl(const casadi::MX& x, const casadi::MX& xdot,
            casadi::MX& defects) const = 0;
    virtual void calcInterpolatingControlsImpl(const casadi::MX& /*controls*/,
            casadi::MX& /*interpControls*/) const {
        OPENSIM_THROW_IF(m_pointsForInterpControls.numel(), OpenSim::Exception,
                "Must provide constraints for interpolating controls.")
    }
    // virtual void calcExtrapolatedControlsImpl(casadi::MX& /*controls*/) const {
    //     bool hasExtrapolatedControls = std::any_of(
    //             m_controlPoints.begin(), m_controlPoints.end(),
    //             [](bool b) { return !b; });
    //     OPENSIM_THROW_IF(hasExtrapolatedControls, OpenSim::Exception,
    //             "Must provide scheme for extrapolated controls.")
    // }

    void transcribe();
    void setObjectiveAndEndpointConstraints();
    void calcDefects() {
        calcDefectsImpl(m_unscaledVars.at(states), m_xdot,
                m_constraints.defects);
    }
    void calcInterpolatingControls() {
        calcInterpolatingControlsImpl(
                m_unscaledVars.at(controls), m_constraints.interp_controls);
    }
    // void calcExtrapolatedControls() {
    //     calcExtrapolatedControlsImpl(m_scaledVars.at(controls));
    // }

    /// Use this function to ensure you iterate through variables in the same
    /// order.
    template <typename T>
    static std::vector<Var> getSortedVarKeys(const Variables<T>& vars) {
        std::vector<Var> keys;
        keys.push_back(initial_time);
        keys.push_back(final_time);
        keys.push_back(parameters);
        keys.push_back(states);
        keys.push_back(controls);
        keys.push_back(multipliers);
        keys.push_back(derivatives);
        // for (const auto& kv : vars) { keys.push_back(kv.first); }
        // std::sort(keys.begin(), keys.end());
        return keys;
    }
    /// Convert the map of variables into a column vector, for passing onto
    /// nlpsol(), etc.
    // template <typename T>
    // static T flattenVariables(const CasOC::Variables<T>& vars) {
    //     std::vector<T> stdvec;
    //     for (const auto& key : getSortedVarKeys(vars)) {
    //         stdvec.push_back(vars.at(key));
    //     }
    //     return T::veccat(stdvec);
    // }
    casadi::MX flattenVariablesMX(const CasOC::VariablesMXVector& vars) const {
        std::vector<casadi::MX> stdvec;
        int N = m_numPointsPerMeshInterval - 1;
        for (int imesh = 0; imesh < m_numMeshIntervals; ++imesh) {
            int igrid = imesh * N;
            for (int i = 0; i < N; ++i) {
                stdvec.push_back(vars.at(initial_time)[igrid + i]);
            }
            for (int i = 0; i < N; ++i) {
                stdvec.push_back(vars.at(final_time)[igrid + i]);
            }
            for (int i = 0; i < N; ++i) {
                stdvec.push_back(vars.at(states)[igrid + i]);
            }
            for (int i = 0; i < N; ++i) {
                stdvec.push_back(vars.at(controls)[igrid + i]);
            }
        }
        stdvec.push_back(vars.at(initial_time)[m_numGridPoints - 1]);
        stdvec.push_back(vars.at(final_time)[m_numGridPoints - 1]);
        stdvec.push_back(vars.at(states)[m_numGridPoints - 1]);
        stdvec.push_back(vars.at(controls)[m_numGridPoints - 1]);

        return casadi::MX::veccat(stdvec);
    }

    casadi::DM flattenVariablesDM(const CasOC::VariablesDM& vars) const {
        std::vector<casadi::DM> stdvec;
        int N = m_numPointsPerMeshInterval - 1;
        for (int imesh = 0; imesh < m_numMeshIntervals; ++imesh) {
            int igrid = imesh * N;
            for (int i = 0; i < N; ++i) {
                stdvec.push_back(vars.at(initial_time)(casadi::Slice(), igrid + i));
            }
            for (int i = 0; i < N; ++i) {
                stdvec.push_back(vars.at(final_time)(casadi::Slice(), igrid + i));
            }
            for (int i = 0; i < N; ++i) {
                stdvec.push_back(vars.at(states)(casadi::Slice(), igrid + i));
            }
            for (int i = 0; i < N; ++i) {
                stdvec.push_back(vars.at(controls)(casadi::Slice(), igrid + i));
            }
        }
        stdvec.push_back(vars.at(initial_time)(casadi::Slice(), m_numGridPoints - 1));
        stdvec.push_back(vars.at(final_time)(casadi::Slice(), m_numGridPoints - 1));
        stdvec.push_back(vars.at(states)(casadi::Slice(), m_numGridPoints - 1));
        stdvec.push_back(vars.at(controls)(casadi::Slice(), m_numGridPoints - 1));

        return casadi::DM::veccat(stdvec);
    }
    /// Convert the 'x' column vector into separate variables.
    CasOC::VariablesDM expandVariables(const casadi::DM& x) const {
        CasOC::VariablesDM out;
        using casadi::Slice;
        casadi_int offset = 0;
        for (const auto& key : getSortedVarKeys(m_scaledVars)) {
            const auto& value = m_scaledVars.at(key);
            // Convert a portion of the column vector into a matrix.
            out[key] = casadi::DM::reshape(
                    x(Slice(offset, offset + value.numel())), value.rows(),
                    value.columns());
            offset += value.numel();
        }
        return out;
    }

    /// unscaled = (upper - lower) * scaled - 0.5 * (upper + lower);
    template <typename T>
    Variables<T> unscaleVariables(const Variables<T>& scaledVars) {
        using casadi::DM;
        Variables<T> out;

        for (const auto& kv : scaledVars) {
            const auto& key = kv.first;
            const auto& scaled = scaledVars.at(key);
            const auto& numCols = scaled.columns();
            // The shift and scale are column vectors. For appropriate
            // elementwise math, we repeat the column to match the number of
            // columns for this key.
            const auto& shift = DM::repmat(m_shift.at(key), 1, numCols);
            const auto& scale = DM::repmat(m_scale.at(key), 1, numCols);
            out[key] = scaled * scale + shift;
        }
        return out;
    }

    /// scaled = [unscaled + 0.5 * (upper + lower)] / (upper - lower)
    template <typename T>
    Variables<T> scaleVariables(const Variables<T>& unscaledVars) {
        using casadi::DM;
        Variables<T> out;

        for (const auto& kv : unscaledVars) {
            const auto& key = kv.first;
            const auto& unscaled = unscaledVars.at(key);
            const auto& numCols = unscaled.columns();
            // The shift and scale are column vectors. For appropriate
            // elementwise math, we repeat the column to match the number of
            // columns for this key.
            const auto& shift = DM::repmat(m_shift.at(key), 1, numCols);
            const auto& scale = DM::repmat(m_scale.at(key), 1, numCols);
            out[key] = (unscaled - shift) / scale;
        }
        return out;
    }

    /// Flatten the constraints into a row vector, keeping constraints
    /// grouped together by time. Organizing the sparsity of the Jacobian
    /// this way might have benefits for sparse linear algebra.
    template <typename T>
    T flattenConstraints(const Constraints<T>& constraints) const {
        T flat = T(casadi::Sparsity::dense(m_numConstraints, 1));

        int iflat = 0;
        auto copyColumn = [&flat, &iflat](const T& matrix, int columnIndex) {
            using casadi::Slice;
            if (matrix.rows()) {
                flat(Slice(iflat, iflat + matrix.rows())) =
                        matrix(Slice(), columnIndex);
                iflat += matrix.rows();
            }
        };

        // Trapezoidal sparsity pattern (mapping between flattened and expanded
        // constraints) for mesh intervals 0, 1 and 2: Endpoint constraints
        // depend on all time points through their integral.
        //
        //                   0    1    2    3
        //    endpoint       x    x    x    x
        //    defect_0       x    x
        //    residual_0     x
        //    kinematic_0    x
        //    path_0         x
        //    defect_1            x    x
        //    residual_1          x
        //    kinematic_1         x
        //    path_1              x
        //    defect_2                 x    x
        //    residual_2               x
        //    kinematic_2              x
        //    path_2                   x
        //    residual_3                    x
        //    kinematic_3                   x
        //    path_3                        x

        // Hermite-Simpson sparsity pattern for mesh intervals 0, 1 and 2.
        // '*' indicates additional non-zero entry when path constraint
        // mesh interior points are enforced. This sparsity pattern also applies
        // to the Legendre-Gauss and Legendre-Gauss-Radau transcription with
        // polynomial degree equal to 1.
        //
        //                   0    0.5    1    1.5    2    2.5    3
        //    endpoint       x     x     x     x     x     x     x
        //    defect_0       x     x     x
        //    residual_0     x     x
        //    kinematic_0    x
        //    path_0         x     *
        //    interp_con_0   x     x     x
        //    defect_1                   x     x     x
        //    residual_1                 x     x
        //    kinematic_1                x
        //    path_1                     x     *
        //    interp_con_1               x     x     x
        //    defect_2                               x     x     x
        //    residual_2                             x     x
        //    kinematic_2                            x
        //    path_2                                 x     *
        //    interp_con_2                           x     x     x
        //    residual_3                                         x
        //    kinematic_3                                        x
        //    path_3                                             x
        //                   0    0.5    1    1.5    2    2.5    3

        for (const auto& endpoint : constraints.endpoint) {
            copyColumn(endpoint, 0);
        }

        // Constraints for each mesh interval.
        int N = m_numPointsPerMeshInterval - 1;
        int icon = 0;
        int itime_i = 0;
        int itime_f = 0;
        int iparam = 0;
        for (int imesh = 0; imesh < m_numMeshIntervals; ++imesh) {
            int igrid = imesh * N;

            // Time constraints.
            for (int i = 0; i < N; ++i) {
                copyColumn(constraints.initial_time, itime_i++);
            }
            for (int i = 0; i < N; ++i) {
                copyColumn(constraints.final_time, itime_f++);
            }

            // Parameter constraints.
            for (int i = 0; i < N; ++i) {
                copyColumn(constraints.parameters, iparam++);
            }

            // Defect constraints.
            copyColumn(constraints.defects, imesh);

            // Multibody and auxiliary residuals.
            for (int i = 0; i < N; ++i) {
                copyColumn(constraints.multibody_residuals, igrid + i);
                copyColumn(constraints.auxiliary_residuals, igrid + i);
            }

            // Kinematic constraints.
            copyColumn(constraints.kinematic, imesh);

            // Path constraints.
            if (m_solver.getEnforcePathConstraintMidpoints()) {
                for (int i = 0; i < N; ++i) {
                    for (const auto& path : constraints.path) {
                        copyColumn(path, igrid + i);
                    }
                }
            } else {
                for (const auto& path : constraints.path) {
                    copyColumn(path, imesh);
                }
            }

            // Interpolating controls.
            if (m_pointsForInterpControls.numel()) {
                for (int i = 0; i < N-1; ++i) {
                    copyColumn(constraints.interp_controls, icon++);
                }
            }
        }

        // Final grid point.
        copyColumn(constraints.multibody_residuals, m_numGridPoints - 1);
        copyColumn(constraints.auxiliary_residuals, m_numGridPoints - 1);
        copyColumn(constraints.kinematic, m_numMeshPoints - 1);
        if (m_solver.getEnforcePathConstraintMidpoints()) {
            for (const auto& path : constraints.path) {
                copyColumn(path, m_numGridPoints - 1);
            }
        } else {
            for (const auto& path : constraints.path) {
                copyColumn(path, m_numMeshPoints - 1);
            }
        }

        OPENSIM_THROW_IF(iflat != m_numConstraints, OpenSim::Exception,
                "Internal error: final value of the index into the flattened "
                "constraints should be equal to the number of constraints.");
        return flat;
    }

    // Expand constraints that have been flattened into a Constraints struct.
    template <typename T>
    Constraints<T> expandConstraints(const T& flat) const {
        using casadi::Sparsity;

        // Allocate memory.
        auto init = [](int numRows, int numColumns) {
            return T(casadi::Sparsity::dense(numRows, numColumns));
        };
        Constraints<T> out;
        out.initial_time = init(1, m_numParameterConstraints);
        out.final_time = init(1, m_numParameterConstraints);
        out.parameters = init(m_numParameterConstraints, m_numMeshPoints - 1);
        out.defects = init(m_numDefectsPerMeshInterval, m_numMeshPoints - 1);
        out.multibody_residuals = init(m_numMultibodyResiduals,
                m_numGridPoints);
        out.auxiliary_residuals = init(m_numAuxiliaryResiduals,
                m_numGridPoints);
        out.kinematic = init(m_problem.getNumKinematicConstraintEquations(),
                m_numMeshPoints);
        out.endpoint.resize(m_problem.getEndpointConstraintInfos().size());
        for (int iec = 0; iec < (int)m_constraints.endpoint.size(); ++iec) {
            const auto& info = m_problem.getEndpointConstraintInfos()[iec];
            out.endpoint[iec] = init(info.num_outputs, 1);
        }
        out.path.resize(m_problem.getPathConstraintInfos().size());
        for (int ipc = 0; ipc < (int)m_constraints.path.size(); ++ipc) {
            const auto& info = m_problem.getPathConstraintInfos()[ipc];
            out.path[ipc] = init(info.size(), m_numPathConstraintPoints);
        }
        out.interp_controls = init(m_problem.getNumControls(),
                (int)m_pointsForInterpControls.numel());

        int iflat = 0;
        auto copyColumn = [&flat, &iflat](T& matrix, int columnIndex) {
            using casadi::Slice;
            if (matrix.rows()) {
                matrix(Slice(), columnIndex) =
                        flat(Slice(iflat, iflat + matrix.rows()));
                iflat += matrix.rows();
            }
        };

        for (auto& endpoint : out.endpoint) {
            copyColumn(endpoint, 0);
        }

        // Constraints for each mesh interval.
        int N = m_numPointsPerMeshInterval - 1;
        int icon = 0;
        int itime_i = 0;
        int itime_f = 0;
        int iparam = 0;
        for (int imesh = 0; imesh < m_numMeshIntervals; ++imesh) {
            int igrid = imesh * N;

            // Time constraints.
            for (int i = 0; i < N; ++i) {
                copyColumn(out.initial_time, itime_i++);
            }
            for (int i = 0; i < N; ++i) {
                copyColumn(out.final_time, itime_f++);
            }

            // Parameter constraints.
            for (int i = 0; i < N; ++i) {
                copyColumn(out.parameters, iparam++);
            }

            // Defect constraints.
            copyColumn(out.defects, imesh);

            // Multibody and auxiliary residuals.
            for (int i = 0; i < N; ++i) {
                copyColumn(out.multibody_residuals, igrid + i);
                copyColumn(out.auxiliary_residuals, igrid + i);
            }

            // Kinematic constraints.
            copyColumn(out.kinematic, imesh);

            // Path constraints.
            if (m_solver.getEnforcePathConstraintMidpoints()) {
                for (int i = 0; i < N; ++i) {
                    for (auto& path : out.path) {
                        copyColumn(path, igrid + i);
                    }
                }
            } else {
                for (auto& path : out.path) {
                    copyColumn(path, imesh);
                }
            }

            // Interpolating controls.
            if (m_pointsForInterpControls.numel()) {
                for (int i = 0; i < N-1; ++i) {
                    copyColumn(out.interp_controls, icon++);
                }
            }
        }

        // Final grid point.
        copyColumn(out.multibody_residuals, m_numGridPoints - 1);
        copyColumn(out.auxiliary_residuals, m_numGridPoints - 1);
        copyColumn(out.kinematic, m_numMeshPoints - 1);
        if (m_solver.getEnforcePathConstraintMidpoints()) {
            for (auto& path : out.path) {
                copyColumn(path, m_numGridPoints - 1);
            }
        } else {
            for (auto& path : out.path) {
                copyColumn(path, m_numMeshPoints - 1);
            }
        }

        OPENSIM_THROW_IF(iflat != m_numConstraints, OpenSim::Exception,
                "Internal error: final value of the index into the flattened "
                "constraints should be equal to the number of constraints.");
        return out;
    }

    ObjectiveBreakdown expandObjectiveTerms(const casadi::DM& terms) const {
        ObjectiveBreakdown out;
        for (int io = 0; io < (int)m_objectiveTermNames.size(); ++io) {
            out.push_back(std::make_pair(
                    m_objectiveTermNames[io], terms(io).scalar()));
        }
        return out;
    }


    friend class NlpsolCallback;
};

} // namespace CasOC

#endif // OPENSIM_CASOCTRANSCRIPTION_H
