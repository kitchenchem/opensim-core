/* -------------------------------------------------------------------------- *
 *                    OpenSim:  ActuatorInputController.cpp                   *
 * -------------------------------------------------------------------------- *
 * The OpenSim API is a toolkit for musculoskeletal modeling and simulation.  *
 * See http://opensim.stanford.edu and the NOTICE file for more information.  *
 * OpenSim is developed at Stanford University and supported by the US        *
 * National Institutes of Health (U54 GM072970, R24 HD065690) and by DARPA    *
 * through the Warrior Web program.                                           *
 *                                                                            *
 * Copyright (c) 2005-2024 Stanford University and the Authors                *
 * Author(s): Nicholas Bianco                                                 *
 *                                                                            *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may    *
 * not use this file except in compliance with the License. You may obtain a  *
 * copy of the License at http://www.apache.org/licenses/LICENSE-2.0.         *
 *                                                                            *
 * Unless required by applicable law or agreed to in writing, software        *
 * distributed under the License is distributed on an "AS IS" BASIS,          *
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.   *
 * See the License for the specific language governing permissions and        *
 * limitations under the License.                                             *
 * -------------------------------------------------------------------------- */

#include "ActuatorInputController.h"

using namespace OpenSim;

//=============================================================================
// CONSTRUCTOR(S) AND DESTRUCTOR
//=============================================================================
ActuatorInputController::ActuatorInputController() : InputController() {}

ActuatorInputController::~ActuatorInputController() = default;

ActuatorInputController::ActuatorInputController(
        const ActuatorInputController& other) = default;

ActuatorInputController& ActuatorInputController::operator=(
        const ActuatorInputController& other) = default;

ActuatorInputController::ActuatorInputController(
        ActuatorInputController&& other) = default;

ActuatorInputController& ActuatorInputController::operator=(
        ActuatorInputController&& other) = default;

//=============================================================================
// CONTROLLER INTERFACE
//=============================================================================
void ActuatorInputController::computeControls(const SimTK::State& s,
        SimTK::Vector& controls) const {
    const auto& input = getInput<double>("inputs");
    OPENSIM_ASSERT(input.getNumConnectees() == getNumControls());
    for (int i = 0; i < static_cast<int>(input.getNumConnectees()); ++i) {
        controls[m_controlIndexesInConnecteeOrder[i]] = input.getValue(s, i);
    }
}

//=============================================================================
// INPUT CONTROLLER INTERFACE
//=============================================================================
std::vector<std::string>
ActuatorInputController::getExpectedInputChannelAliases() const {
    std::vector<std::string> aliases;
    const auto& socket = getSocket<Actuator>("actuators");
    for (int i = 0; i < static_cast<int>(socket.getNumConnectees()); ++i) {
        const auto& actu = socket.getConnectee(i);
        if (actu.numControls() > 1) {
            // Non-scalar actuator.
            for (int j = 0; j < actu.numControls(); ++j) {
                // Use the control name format based on
                // SimulationUtilities::createControlNamesFromModel().
                aliases.push_back(
                        fmt::format("{}_{}", actu.getAbsolutePathString(), j));
            }
        } else {
            // Scalar actuator.
            aliases.push_back(actu.getAbsolutePathString());
        }
    }

    return aliases;
}

void ActuatorInputController::checkInputConnections() const {
    const auto& input = getInput<double>("inputs");
    OPENSIM_THROW_IF(
            static_cast<int>(input.getNumConnectees()) != getNumControls(),
            Exception,
            "Expected the number of Input connectees ({}) to match the number "
            "of actuators controls ({}), but they do not.",
            input.getNumConnectees(), getNumControls());
}

//=============================================================================
// MODEL COMPONENT INTERFACE
//=============================================================================
void ActuatorInputController::extendConnectToModel(Model& model) {
    Super::extendConnectToModel(model);

    const auto& controlNames = getControlNames();
    const auto& controlIndexes = getControlIndexes();
    OPENSIM_ASSERT(getNumControls() == static_cast<int>(controlNames.size()))

    const auto& input = getInput<double>("inputs");
    const auto expectedAliases = getExpectedInputChannelAliases();
    std::vector<std::string> aliasesInConnecteeOrder;
    for (int i = 0; i < static_cast<int>(input.getNumConnectees()); ++i) {
        const auto& alias = input.getAlias(i);
        auto it = std::find(expectedAliases.begin(), expectedAliases.end(),
                alias);
        OPENSIM_THROW_IF(it == expectedAliases.end(), Exception,
                "Expected the Input alias '{}' to match a control name for an "
                "actuator in the controller's ActuatorSet, but it does not.",
                alias);
        aliasesInConnecteeOrder.push_back(alias);
    }

    // Use the Input channel aliases to map the Input connectee indexes to
    // the corresponding control indexes in the model control cache. We
    // expect the Input connectee aliases to match the names of the actuator
    // controls in the controller's ActuatorSet.
    m_controlIndexesInConnecteeOrder.clear();
    m_controlIndexesInConnecteeOrder.reserve(getNumControls());
    for (const auto& alias : aliasesInConnecteeOrder) {
        auto it = std::find(controlNames.begin(), controlNames.end(), alias);
        m_controlIndexesInConnecteeOrder.push_back(
            controlIndexes[it - controlNames.begin()]);
    }
}