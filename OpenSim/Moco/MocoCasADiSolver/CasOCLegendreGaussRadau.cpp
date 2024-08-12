/* -------------------------------------------------------------------------- *
 * OpenSim: CasOCLegendreGaussRadau.cpp                                       *
 * -------------------------------------------------------------------------- *
 * Copyright (c) 2023 Stanford University and the Authors                     *
 *                                                                            *
 * Author(s): Nicholas Bianco                                                 *
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
#include "CasOCLegendreGaussRadau.h"

using casadi::DM;
using casadi::MX;
using casadi::Slice;

namespace CasOC {

DM LegendreGaussRadau::createQuadratureCoefficientsImpl() const {

    // The duration of each mesh interval.
    const DM mesh(m_solver.getMesh());
    const DM meshIntervals = mesh(Slice(1, m_numMeshPoints)) -
                             mesh(Slice(0, m_numMeshPoints - 1));
    const DM w = m_quadratureCoefficients;

    // Loop through each mesh interval and update the corresponding
    // components in the total coefficients vector.
    DM quadCoeffs(m_numGridPoints, 1);
    for (int imesh = 0; imesh < m_numMeshIntervals; ++imesh) {
        const int igrid = imesh * m_degree;
        for (int d = 0; d < m_degree; ++d) {
            quadCoeffs(igrid + d + 1) += w(d) * meshIntervals(imesh);
        }
    }
    return quadCoeffs;
}

DM LegendreGaussRadau::createMeshIndicesImpl() const {
    DM indices = DM::zeros(1, m_numGridPoints);
    for (int imesh = 0; imesh < m_numMeshIntervals; ++imesh) {
        indices(imesh * m_degree) = 1;
    }
    indices(m_numGridPoints - 1) = 1;
    return indices;
}

DM LegendreGaussRadau::createControlIndicesImpl() const {
    DM indices = DM::ones(1, m_numGridPoints);
    indices(0) = 0;
    return indices;
}

void LegendreGaussRadau::calcDefectsImpl(const casadi::MX& x,
        const casadi::MX& xdot, const casadi::MX& ti, const casadi::MX& tf,
        const casadi::MX& p, casadi::MX& defects) const {
    // For more information, see doxygen documentation for the class.

    const int NS = m_problem.getNumStates();
    const int NP = m_problem.getNumParameters();
    for (int imesh = 0; imesh < m_numMeshIntervals; ++imesh) {
        const int igrid = imesh * m_degree;
        const auto h = m_times(igrid + m_degree) - m_times(igrid);
        const auto x_i = x(Slice(), Slice(igrid, igrid + m_degree + 1));
        const auto xdot_i = xdot(Slice(),
                Slice(igrid + 1, igrid + m_degree + 1));

        // Time variables.
        defects(Slice(0, 1), imesh) = ti(imesh + 1) - ti(imesh);
        defects(Slice(1, 2), imesh) = tf(imesh + 1) - tf(imesh);

        // Parameters.
        defects(Slice(2, 2 + NP), imesh) =
                p(Slice(), imesh + 1) - p(Slice(), imesh);

        // Residual function defects.
        MX residual = h * xdot_i - MX::mtimes(x_i, m_differentiationMatrix);
        for (int d = 0; d < m_degree; ++d) {
            const int istart = d * NS + 2 + NP;
            const int iend = (d + 1) * NS + 2 + NP;
            defects(Slice(istart, iend), imesh) = residual(Slice(), d);
        }
    }
}

void LegendreGaussRadau::calcInterpolatingControlsImpl(
            casadi::MX& controls) const {
    // if (m_problem.getNumControls() &&
    //         m_solver.getInterpolateControlMidpoints()) {
    //     for (int imesh = 0; imesh < m_numMeshIntervals; ++imesh) {
    //         const int igrid = imesh * m_degree;
    //         const auto c_i = controls(Slice(), igrid);
    //         const auto c_ip1 = controls(Slice(), igrid + m_degree);
    //         for (int d = 0; d < m_degree-1; ++d) {
    //             const auto c_t = controls(Slice(), igrid + d + 1);
    //             interpControls(Slice(), imesh * (m_degree - 1) + d) =
    //                     c_t - (m_legendreRoots[d] * (c_ip1 - c_i) + c_i);
    //         }
    //     }
    // }
}

std::vector<std::pair<Var, int>> LegendreGaussRadau::getVariableOrder() const {
    std::vector<std::pair<Var, int>> order;
    int N = m_numPointsPerMeshInterval - 1;
    for (int imesh = 0; imesh < m_numMeshIntervals; ++imesh) {
        int igrid = imesh * N;
        order.push_back({states, igrid});
        order.push_back({initial_time, imesh});
        order.push_back({final_time, imesh});
        order.push_back({parameters, imesh});
        for (int i = 1; i < N; ++i) {
            order.push_back({states, igrid + i});
        }
        if (m_solver.getInterpolateControlMidpoints()) {
            for (int d = 0; d < m_degree; ++d) {
                order.push_back({controls, m_degree * imesh + d});
            }
        } else {
            for (int i = 0; i < N; ++i) {
                order.push_back({controls, igrid + i});
            }
        }
        for (int i = 0; i < N; ++i) {
            order.push_back({multipliers, igrid + i});
        }
        for (int i = 0; i < N; ++i) {
            order.push_back({derivatives, igrid + i});
        }
        order.push_back({slacks, imesh});
    }
    order.push_back({states, m_numGridPoints - 1});
    order.push_back({initial_time, m_numMeshIntervals});
    order.push_back({final_time, m_numMeshIntervals});
    order.push_back({parameters, m_numMeshIntervals});
    if (!m_solver.getInterpolateControlMidpoints()) {
        order.push_back({controls, m_numGridPoints - 1});
    }
    order.push_back({multipliers, m_numGridPoints - 1});
    order.push_back({derivatives, m_numGridPoints - 1});

    return order;
}

} // namespace CasOC