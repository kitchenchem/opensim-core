/* -------------------------------------------------------------------------- *
 * OpenSim: CasOCLegendreGauss.cpp                                            *
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
#include "CasOCLegendreGauss.h"

using casadi::DM;
using casadi::MX;
using casadi::Slice;

namespace CasOC {

DM LegendreGauss::createQuadratureCoefficientsImpl() const {

    // The duration of each mesh interval.
    const DM mesh(m_solver.getMesh());
    const DM meshIntervals = mesh(Slice(1, m_numMeshPoints)) -
                             mesh(Slice(0, m_numMeshPoints - 1));
    const DM w = m_quadratureCoefficients;

    // Loop through each mesh interval and update the corresponding
    // components in the total coefficients vector.
    DM quadCoeffs(m_numGridPoints, 1);
    for (int imesh = 0; imesh < m_numMeshIntervals; ++imesh) {
        const int igrid = imesh * (m_degree + 1);
        // There are no quadrature coefficients at the mesh points (i.e.,
        // quadCoeffs(igrid) = 0).
        for (int d = 0; d < m_degree; ++d) {
            quadCoeffs(igrid + d + 1) += w(d) * meshIntervals(imesh);
        }
    }
    return quadCoeffs;
}

DM LegendreGauss::createMeshIndicesImpl() const {
    DM indices = DM::zeros(1, m_numGridPoints);
    for (int imesh = 0; imesh < m_numMeshIntervals; ++imesh) {
        indices(imesh * (m_degree + 1)) = 1;
    }
    indices(m_numGridPoints - 1) = 1;
    return indices;
}

DM LegendreGauss::createControlIndicesImpl() const {
    DM indices = DM::zeros(1, m_numGridPoints);
    for (int imesh = 0; imesh < m_numMeshIntervals; ++imesh) {
        const int igrid = imesh * (m_degree + 1);
        for (int d = 0; d < m_degree; ++d) {
            indices(igrid + d + 1) = 1;
        }
    }
    return indices;
}

void LegendreGauss::calcDefectsImpl(const casadi::MXVector& x, 
        const casadi::MXVector& xdot, casadi::MX& defects) const {
    // For more information, see doxygen documentation for the class.

    const int NS = m_problem.getNumStates();
    const int NP = m_problem.getNumParameters();
    for (int imesh = 0; imesh < m_numMeshIntervals; ++imesh) {
        const int igrid = imesh * (m_degree + 1);
        const auto h = m_intervals(imesh);
        const auto x_i = x[imesh](Slice(), Slice(0, m_degree + 1));
        const auto xdot_i = xdot[imesh](Slice(), Slice(1, m_degree + 1));
        const auto x_ip1 = x[imesh](Slice(), m_degree + 1);

        // End state interpolation.
        defects(Slice(0, NS), imesh) =
                x_ip1 - MX::mtimes(x_i, m_interpolationCoefficients);

        // Residual function defects.
        MX residual = h * xdot_i - MX::mtimes(x_i, m_differentiationMatrix);
        for (int d = 0; d < m_degree; ++d) {
            const int istart = (d + 1) * NS;
            const int iend = (d + 2) * NS;
            defects(Slice(istart, iend), imesh) = residual(Slice(), d);
        }
    }
}

void LegendreGauss::calcInterpolatingControlsImpl(casadi::MX& controls) const {
    calcInterpolatingControlsHelper(controls);
}

void LegendreGauss::calcInterpolatingControlsImpl(casadi::DM& controls) const {
    calcInterpolatingControlsHelper(controls);
}

std::vector<std::pair<Var, int>> LegendreGauss::getVariableOrder() const {
    std::vector<std::pair<Var, int>> order;
    int N = m_numPointsPerMeshInterval - 1;
    for (int imesh = 0; imesh < m_numMeshIntervals; ++imesh) {
        int igrid = imesh * N;
        order.push_back({initial_time, imesh});
        order.push_back({final_time, imesh});
        order.push_back({parameters, imesh});
        if (imesh > 0) {
            order.push_back({projection_states, imesh-1});
            order.push_back({slacks, imesh-1});
        }
        for (int i = 0; i < N; ++i) {
            order.push_back({states, igrid + i});
        }
        if (m_solver.getInterpolateControlMeshInteriorPoints()) {
            for (int d = 0; d < m_degree; ++d) {
                order.push_back({controls, igrid + d + 1});
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
    }
    order.push_back({initial_time, m_numMeshIntervals});
    order.push_back({final_time, m_numMeshIntervals});
    order.push_back({parameters, m_numMeshIntervals});
    order.push_back({projection_states, m_numMeshIntervals-1});
    order.push_back({slacks, m_numMeshIntervals-1});
    order.push_back({states, m_numGridPoints - 1});
    if (!m_solver.getInterpolateControlMeshInteriorPoints()) {
        order.push_back({controls, m_numGridPoints - 1});
    }
    order.push_back({multipliers, m_numGridPoints - 1});
    order.push_back({derivatives, m_numGridPoints - 1});

    return order;
}

} // namespace CasOC
