#include <casadi/casadi.hpp>
#include <OpenSim/Moco/osimMoco.h>

using namespace casadi;
using namespace OpenSim;

/// Translate a point mass in one dimension in minimum time. This is a very
/// simple example that shows only the basics of Moco.
///
/// @verbatim
/// minimize   t_f
/// subject to xdot = v
///            vdot = F/m
///            x(0)   = 0
///            x(t_f) = 1
///            v(0)   = 0
///            v(t_f) = 0
/// w.r.t.     x   in [-5, 5]    position of mass
///            v   in [-50, 50]  speed of mass
///            F   in [-50, 50]  force applied to the mass
///            t_f in [0, 5]     final time
/// constants  m       mass
/// @endverbatim

class CustomFunction : public casadi::Callback {
public:
    virtual ~CustomFunction() = default;
    virtual void constructFunction(const std::string& name, 
            bool enableFiniteDifference,
            const std::string& finiteDiffScheme, 
            const double& mass) {
        m_mass = mass;
        casadi::Dict opts;
        opts["enable_fd"] = enableFiniteDifference;
        opts["fd_method"] = finiteDiffScheme;
        this->construct(name, opts);
    }

    casadi_int get_n_in() override { return 3; }
    std::string get_name_in(casadi_int i) override {
        switch (i) {
        case 0: return "time";
        case 1: return "states";
        case 2: return "controls";
        default: OPENSIM_THROW(OpenSim::Exception, "Internal error.");
        }
    }
    casadi::Sparsity get_sparsity_in(casadi_int i) override {
        if (i == 0) {
            return casadi::Sparsity::dense(1, 1);
        } else if (i == 1) {
            return casadi::Sparsity::dense(2, 1);
        } else if (i == 2) {
            return casadi::Sparsity::dense(1, 1);
        } else {
            return casadi::Sparsity(0, 0);
        }
    }

protected:
    double m_mass = 1.0;
};


class MultibodySystem : public CustomFunction {
public:
    casadi_int get_n_out() override final { return 1; }
    std::string get_name_out(casadi_int i) override final {
        switch (i) {
        case 0: return "multibody_derivatives";
        default: OPENSIM_THROW(OpenSim::Exception, "Internal error.");
        }
    }
    casadi::Sparsity get_sparsity_out(casadi_int i) override final {
        if (i == 0) {
            return casadi::Sparsity::dense(1, 1); // numSpeeds x 1
        } else {
            return casadi::Sparsity(0, 0);
        }
    }
    DMVector eval(const DMVector& args) const override {
        DM controls = args.at(2);
        DMVector out((int)n_out());
        out[0] = controls / m_mass;
        return out;
    }
};

class MultibodySystemWithJacobian : public CustomFunction {
public:
    casadi_int get_n_out() override final { return 1; }
    std::string get_name_out(casadi_int i) override final {
        switch (i) {
        case 0: return "multibody_derivatives";
        default: OPENSIM_THROW(OpenSim::Exception, "Internal error.");
        }
    }
    casadi::Sparsity get_sparsity_out(casadi_int i) override final {
        if (i == 0) {
            return casadi::Sparsity::dense(1, 1); // numSpeeds x 1
        } else {
            return casadi::Sparsity(0, 0);
        }
    }
    DMVector eval(const DMVector& args) const override {
        DM controls = args.at(2);
        DMVector out((int)n_out());
        out[0] = controls / m_mass;
        return out;
    }
    bool has_jacobian() const override { return true; }
    casadi::Function get_jacobian(const std::string& name,
            const std::vector<std::string>& inames,
            const std::vector<std::string>& onames, 
            const casadi::Dict& opts) const override {
        MultibodySystemJacobian jac("multibody_system_jacobian", opts, m_mass);
        return jac;
    }
    
private:
    class MultibodySystemJacobian : public casadi::Callback {
    public:
        MultibodySystemJacobian(const std::string& name, 
                const casadi::Dict& opts, double mass) {
            m_mass = mass;
            this->construct(name, opts);
        }

        virtual ~MultibodySystemJacobian() = default;

        casadi_int get_n_in() override final { return 4; }
        casadi_int get_n_out() override final { return 3; }
        
        casadi::Sparsity get_sparsity_in(casadi_int i) override final {
            if (i == 0) {
                return casadi::Sparsity::dense(1, 1); // nominal input
            } else if (i == 1) {
                return casadi::Sparsity::dense(2, 1); // nominal input
            } else if (i == 2) {
                return casadi::Sparsity::dense(1, 1); // nominal input
            } else if (i == 3) {
                return casadi::Sparsity::dense(1, 1); // nominal output
            } else {
                return casadi::Sparsity(0, 0);
            }
        }

        casadi::Sparsity get_sparsity_out(casadi_int i) override final {
            if (i == 0) {
                return casadi::Sparsity::dense(1, 1);
            } else if (i == 1) {
                return casadi::Sparsity::dense(1, 2); 
            } else if (i == 2) {
                return casadi::Sparsity::dense(1, 1);
            } else {
                return casadi::Sparsity(0, 0);
            }
        }

        DMVector eval(const DMVector& args) const override {
            DMVector out((int)n_out());
            out[0] = DM::zeros(1, 1);
            out[1] = DM::zeros(1, 2);
            out[2] = DM::ones(1, 1);
            out[2] = out[2] / m_mass;
            return out;
        }
    private:
        double m_mass = 1.0;
    };
};

class TranscriptionSlidingMass {
public:
    TranscriptionSlidingMass(double mass, int numMeshIntervals)
            : m_mass(mass), m_numMeshIntervals(numMeshIntervals) {

        // Construct the multibody system function.
        this->m_multibodySystem = OpenSim::make_unique<MultibodySystem>();
        this->m_multibodySystem->constructFunction(
                "multibody_system", true, "central", m_mass);

        this->m_multibodySystemJac = OpenSim::make_unique<MultibodySystemWithJacobian>();
        this->m_multibodySystemJac->constructFunction(
                "multibody_system_with_jacobian", true, "central", m_mass);

        // Transcription scheme info (Legendre-Gauss).
        m_numGridPoints = m_numMeshIntervals + 1;
        m_numDefectsPerMeshInterval = m_numStates;
        m_numConstraints = m_numStates * m_numMeshIntervals;

         // Create the mesh.
        for (int i = 0; i < (numMeshIntervals + 1); ++i) {
            m_mesh.push_back(i / (double)(numMeshIntervals));
        }

        // Create the grid.
        m_grid = m_mesh;

        auto makeTimeIndices = [](const std::vector<int>& in) {
            casadi::Matrix<casadi_int> out(1, in.size());
            for (int i = 0; i < (int)in.size(); ++i) { out(i) = in[i]; }
            return out;
        };
        std::vector<int> gridIndicesVector(m_numGridPoints);
        std::iota(gridIndicesVector.begin(), gridIndicesVector.end(), 0);
        m_gridIndices = makeTimeIndices(gridIndicesVector);

        // Create variables and set bounds.
        createVariablesAndSetBounds();
    }

    void createVariablesAndSetBounds() {
        // Create variables.
        m_variables[initial_time] = MX::sym("initial_time");
        m_variables[final_time] = MX::sym("final_time");
        m_variables[states] = MX::sym("states", m_numStates, m_numGridPoints);
        m_variables[controls] = MX::sym("controls", m_numControls, 
                m_numGridPoints);

        // Create the time vector.
        m_times = createTimes(m_variables[initial_time], m_variables[final_time]);
        m_duration = m_variables[final_time] - m_variables[initial_time];


        // Set variable bounds.
        auto initializeBoundsDM = [&](VariablesDM& bounds) {
        for (auto& kv : m_variables) {
                bounds[kv.first] = DM(kv.second.rows(), kv.second.columns());
            }
        };
        initializeBoundsDM(m_lowerBounds);
        initializeBoundsDM(m_upperBounds);

        setVariableBounds(initial_time, 0, 0, {0, 0});
        setVariableBounds(final_time, 0, 0, {0, 5});

        setVariableBounds(states, 0, 0, {0, 0});
        setVariableBounds(states, 0, -1, {1, 1});
        setVariableBounds(states, 0, Slice(1, m_numGridPoints-1), {-5, 5});

        setVariableBounds(states, 1, 0, {0, 0});
        setVariableBounds(states, 1, -1, {0, 0});
        setVariableBounds(states, 1, Slice(1, m_numGridPoints-1), {-50, 50});

        setVariableBounds(controls, Slice(), Slice(), {-50, 50});
    }

    void setObjective() {
        m_objective = m_variables[final_time];
    }

    void transcribe() {
        // Cost.
        // =====
        setObjective(); 

        // Defects.
        // ========
        const int NS = m_numStates;
        const int NC = m_numControls;
        m_xdot = MX(NS, m_numGridPoints);
        m_constraints.defects = MX(casadi::Sparsity::dense(
                m_numDefectsPerMeshInterval, m_numMeshIntervals));
        m_constraintsLowerBounds.defects =
                DM::zeros(m_numDefectsPerMeshInterval, m_numMeshIntervals);
        m_constraintsUpperBounds.defects =
                DM::zeros(m_numDefectsPerMeshInterval, m_numMeshIntervals);

        calcStateDerivativesCallbackWithJac(m_variables[states], m_variables[controls], 
                m_xdot);

        calcDefects(m_variables[states], m_xdot, m_constraints.defects);
    }

    void calcStateDerivativesSymbolic(const MX& x, const MX& c, MX& xdot) {
        xdot(0, Slice()) = x(1, Slice());
        xdot(1, Slice()) = c(0, Slice()) / m_mass;
    }

    void calcStateDerivativesCallback(const MX& x, const MX& c, MX& xdot) {
        xdot(0, Slice()) = x(1, Slice());

        std::vector<Var> inputs{states, controls};
        const auto out = evalOnTrajectory(
                    *m_multibodySystem, inputs, m_gridIndices);
        xdot(1, Slice()) = out.at(0);
    }

    void calcStateDerivativesCallbackWithJac(const MX& x, const MX& c, MX& xdot) {
        xdot(0, Slice()) = x(1, Slice());

        std::vector<Var> inputs{states, controls};
        const auto out = evalOnTrajectory(
                    *m_multibodySystemJac, inputs, m_gridIndices);
        xdot(1, Slice()) = out.at(0);
    }

    void calcDefects(const casadi::MX& x, const casadi::MX& xdot, 
            casadi::MX& defects) {

        const int NS = m_numStates;
        for (int imesh = 0; imesh < m_numMeshIntervals; ++imesh) {
            const auto h = m_times(imesh + 1) - m_times(imesh);
            const auto x_i = x(Slice(), imesh);
            const auto x_ip1 = x(Slice(), imesh + 1);
            const auto xdot_i = xdot(Slice(), imesh);
            const auto xdot_ip1 = xdot(Slice(), imesh + 1);

            // Trapezoidal defects.
            defects(Slice(), imesh) = 
                    x_ip1 - (x_i + 0.5 * h * (xdot_ip1 + xdot_i));
        }
    }

    MocoTrajectory solve() {
        // Define the NLP.
        // ---------------
        transcribe();

        // Create a guess.
        // ---------------
        VariablesDM guess;
        guess[initial_time] = 0;
        guess[final_time] = 1;
        guess[states] = DM::zeros(m_numStates, m_numGridPoints);
        guess[controls] = DM::zeros(m_numControls, m_numGridPoints);

        auto x = flattenVariables(m_variables);
        casadi_int numVariables = x.numel();

        auto g = flattenConstraints(m_constraints);
        casadi_int numConstraints = g.numel();

        casadi::MXDict nlp;
        nlp.emplace(std::make_pair("x", x));
        nlp.emplace(std::make_pair("f", m_objective));
        nlp.emplace(std::make_pair("g", g));

        casadi::Dict options;
        casadi::Dict solverOptions;
        solverOptions["hessian_approximation"] = "limited-memory";
        options["ipopt"] = solverOptions;

        const casadi::Function nlpFunc = casadi::nlpsol("nlp", "ipopt", nlp, options);

        // Run the optimization (evaluate the CasADi NLP function).
        // --------------------------------------------------------
        // The inputs and outputs of nlpFunc are numeric (casadi::DM).
        const casadi::DMDict nlpResult = nlpFunc(casadi::DMDict{
                    {"x0", flattenVariables(guess)},
                    {"lbx", flattenVariables(m_lowerBounds)},
                    {"ubx", flattenVariables(m_upperBounds)},
                    {"lbg", flattenConstraints(m_constraintsLowerBounds)},
                    {"ubg", flattenConstraints(m_constraintsUpperBounds)}});

        const auto finalVariables = nlpResult.at("x");
        VariablesDM variables = expandVariables(finalVariables);
        DM times = createTimes(variables[initial_time], variables[final_time]);

        DM objective = nlpResult.at("f").scalar();
        std::cout << "Objective: " << objective << std::endl;

        // Create a MocoTrajectory.
        // ------------------------
        SimTK::Vector time(m_numGridPoints, 0.0);
        for (int i = 0; i < m_numGridPoints; ++i) {
            time[i] = times(i).scalar();
        }

        SimTK::Matrix statesTrajectory(m_numGridPoints, m_numStates, 0.0);
        for (int i = 0; i < m_numGridPoints; ++i) {
            for (int j = 0; j < m_numStates; ++j) {
                statesTrajectory(i, j) = variables[states](j, i).scalar();
            }
        }

        SimTK::Matrix controlsTrajectory(m_numGridPoints, m_numControls, 0.0);
        for (int i = 0; i < m_numGridPoints; ++i) {
            for (int j = 0; j < m_numControls; ++j) {
                controlsTrajectory(i, j) = variables[controls](j, i).scalar();
            }
        }

        std::vector<std::string> stateNames = {"position", "speed"};
        std::vector<std::string> controlNames = {"force"};
        MocoTrajectory trajectory(time, stateNames, controlNames, {}, {},
                statesTrajectory, controlsTrajectory, SimTK::Matrix(), 
                SimTK::RowVector());

        return trajectory;
    }

private:
    // Structs
    // -------
    // Variables
    enum Var {
        initial_time,
        final_time,
        states,
        controls,
    };
    template <typename T>
    using Variables = std::unordered_map<Var, T, std::hash<int>>;
    using VariablesDM = Variables<casadi::DM>;
    using VariablesMX = Variables<casadi::MX>;

    // Constraints
    template <typename T>
    struct Constraints {
        T defects;
    };

    // Bounds
    struct Bounds {
        Bounds() = default;
        Bounds(double lower, double upper) : lower(lower), upper(upper) {}
        double lower = std::numeric_limits<double>::quiet_NaN();
        double upper = std::numeric_limits<double>::quiet_NaN();
        bool isSet() const { return !std::isnan(lower) && !std::isnan(upper); }
    };

    // Helper functions
    // ----------------
    template <typename T>
    T createTimes(const T& initialTime, const T& finalTime) const {
        return (finalTime - initialTime) * m_grid + initialTime;
    }

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

    template <typename T>
    static std::vector<Var> getSortedVarKeys(const Variables<T>& vars) {
        std::vector<Var> keys;
        for (const auto& kv : vars) { keys.push_back(kv.first); }
        std::sort(keys.begin(), keys.end());
        return keys;
    }

    template <typename T>
    static T flattenVariables(const Variables<T>& vars) {
        std::vector<T> stdvec;
        for (const auto& key : getSortedVarKeys(vars)) {
            stdvec.push_back(vars.at(key));
        }
        return T::veccat(stdvec);
    }

    VariablesDM expandVariables(const casadi::DM& x) const {
        VariablesDM out;
        using casadi::Slice;
        casadi_int offset = 0;
        for (const auto& key : getSortedVarKeys(m_variables)) {
            const auto& value = m_variables.at(key);
            // Convert a portion of the column vector into a matrix.
            out[key] = casadi::DM::reshape(
                    x(Slice(offset, offset + value.numel())), value.rows(),
                    value.columns());
            offset += value.numel();
        }
        return out;
    }

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

        // Constraints for each mesh interval.
        for (int imesh = 0; imesh < m_numMeshIntervals; ++imesh) {
            copyColumn(constraints.defects, imesh);
        }

        OPENSIM_THROW_IF(iflat != m_numConstraints, OpenSim::Exception,
                "Internal error: final value of the index into the flattened "
                "constraints should be equal to the number of constraints.");
        return flat;
    }

    template <typename T>
    Constraints<T> expandConstraints(const T& flat) const {
        using casadi::Sparsity;

        // Allocate memory.
        auto init = [](int numRows, int numColumns) {
            return T(casadi::Sparsity::dense(numRows, numColumns));
        };
        Constraints<T> out;
        out.defects = init(m_numDefectsPerMeshInterval, m_numMeshIntervals);

        int iflat = 0;
        auto copyColumn = [&flat, &iflat](T& matrix, int columnIndex) {
            using casadi::Slice;
            if (matrix.rows()) {
                matrix(Slice(), columnIndex) =
                        flat(Slice(iflat, iflat + matrix.rows()));
                iflat += matrix.rows();
            }
        };

        for (int imesh = 0; imesh < m_numMeshIntervals; ++imesh) {
            copyColumn(out.defects, imesh);
        }

        OPENSIM_THROW_IF(iflat != m_numConstraints, OpenSim::Exception,
                "Internal error: final value of the index into the flattened "
                "constraints should be equal to the number of constraints.");
        return out;
    }

    casadi::MXVector evalOnTrajectory(
        const casadi::Function& pointFunction, const std::vector<Var>& inputs,
        const casadi::Matrix<casadi_int>& timeIndices) const {
            const auto trajFunc = pointFunction.map(
                    timeIndices.size2(), "serial", 1);

            // Assemble input.
            MXVector mxIn(inputs.size() + 1);
            mxIn[0] = m_times(timeIndices);
            for (int i = 0; i < (int)inputs.size(); ++i) {
                mxIn[i + 1] = m_variables.at(inputs[i])(Slice(), timeIndices); 
            }

            MXVector mxOut;
            trajFunc.call(mxIn, mxOut);
            return mxOut;
        }

    // Member variables
    // ----------------
    double m_mass = 1.0;
    int m_numStates = 2;
    int m_numControls = 1;

    std::vector<double> m_mesh;
    casadi::DM m_grid;

    int m_numMeshIntervals;
    int m_numGridPoints;
    int m_numDefectsPerMeshInterval;
    int m_numConstraints;

    VariablesMX m_variables;
    VariablesDM m_lowerBounds;
    VariablesDM m_upperBounds;
    casadi::MX m_times;
    casadi::MX m_duration;
    casadi::MX m_objective;
    casadi::MX m_xdot;

    Constraints<casadi::MX> m_constraints;
    Constraints<casadi::DM> m_constraintsLowerBounds;
    Constraints<casadi::DM> m_constraintsUpperBounds;

    std::unique_ptr<MultibodySystem> m_multibodySystem;
    std::unique_ptr<MultibodySystemWithJacobian> m_multibodySystemJac;

    casadi::Matrix<casadi_int> m_gridIndices;
};



int main() {
    int num_mesh_intervals = 50;
    double mass = 2.0;
    TranscriptionSlidingMass transcription(mass, num_mesh_intervals);
    MocoTrajectory solution = transcription.solve();
    solution.write("sandboxAutodiff_solution.sto");

    return EXIT_SUCCESS;
}