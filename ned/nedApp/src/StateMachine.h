#ifndef STATE_MACHINE_H
#define STATE_MACHINE_H

#include <map>

template <class State, class Action>
class StateMachine {
    private:
        std::map<State, std::map<Action, State> > m_transitions;
        State m_state;
    public:
        StateMachine(State initState)
            : m_state(initState)
        {}

        void addState(State curState, Action action, State nextState)
        {
            m_transitions[curState][action] = nextState;
        }

        State &getCurrentState()
        {
            return m_state;
        }

        bool transition(Action action)
        {
            return transition(m_state, action);
        }

        bool transition(State state, Action action)
        {
            auto it = m_transitions.find(state);
            if (it != m_transitions.end()) {
                auto jt = it->second.find(action);
                if (jt != it->second.end()) {
                    m_state = jt->second;
                    return true;
                }
            }
            return false;
        }

        bool checkTransition(Action action)
        {
            return checkTransition(m_state, action);
        }

        bool checkTransition(State state, Action action)
        {
            auto it = m_transitions.find(state);
            if (it != m_transitions.end()) {
                auto jt = it->second.find(action);
                if (jt != it->second.end()) {
                    return true;
                }
            }
            return false;
        }
};

#endif // STATE_MACHINE_H
