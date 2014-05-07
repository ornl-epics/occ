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

        State &transition(Action action)
        {
            return transition(m_state, action);
        }

        State &transition(State curState, Action action)
        {
            auto it = m_transitions.find(curState);
            if (it != m_transitions.end()) {
                auto jt = it->second.find(action);
                if (jt != it->second.end()) {
                    m_state = jt->second;
                }
            }
            return m_state;
        }
};

#endif // STATE_MACHINE_H
