#include "undo_manager.hpp"

UndoManager::UndoManager(size_t max_history)
    : m_maxHistory(max_history) {
}

void UndoManager::Clear() {
    m_stack.clear();
    m_current = 0;
    m_toastText.clear();
    m_toastTimer = 0.0f;
}

void UndoManager::Push(const std::string& ini_content, const std::string& action_name) {
    if (ini_content.empty()) return;

    // Do not push identical consecutive snapshots
    if (!m_stack.empty() && m_current < m_stack.size() && m_stack[m_current].ini_content == ini_content) {
        return;
    }

    // Truncate redo history after current pointer
    if (!m_stack.empty() && m_current + 1 < m_stack.size()) {
        m_stack.erase(m_stack.begin() + (m_current + 1), m_stack.end());
    }

    // Push new snapshot
    m_stack.push_back({ ini_content, action_name });

    // Enforce maximum capacity
    if (m_stack.size() > m_maxHistory) {
        m_stack.pop_front();
    }

    m_current = m_stack.size() - 1;
}

bool UndoManager::CanUndo() const {
    return m_current > 0 && !m_stack.empty();
}

bool UndoManager::CanRedo() const {
    return !m_stack.empty() && m_current + 1 < m_stack.size();
}

bool UndoManager::Undo(std::string& out_ini) {
    if (!CanUndo()) return false;

    const std::string prev_action = m_stack[m_current].action_name;
    m_current--;
    out_ini = m_stack[m_current].ini_content;

    m_toastText = "Undo: " + prev_action + " [Ctrl+Z]";
    m_toastTimer = 2.0f;
    return true;
}

bool UndoManager::Redo(std::string& out_ini) {
    if (!CanRedo()) return false;

    m_current++;
    out_ini = m_stack[m_current].ini_content;
    const std::string redone_action = m_stack[m_current].action_name;

    m_toastText = "Redo: " + redone_action + " [Shift+Ctrl+Z]";
    m_toastTimer = 2.0f;
    return true;
}

void UndoManager::Update(float dt) {
    if (m_toastTimer > 0.0f) {
        m_toastTimer -= dt;
        if (m_toastTimer < 0.0f) {
            m_toastTimer = 0.0f;
        }
    }
}

const std::string& UndoManager::GetToastText() const {
    return m_toastText;
}

float UndoManager::GetToastTimer() const {
    return m_toastTimer;
}
