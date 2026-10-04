#pragma once

#include <string>
#include <deque>
#include <cstddef>

struct UndoSnapshot {
    std::string ini_content;
    std::string action_name;
};

class UndoManager {
public:
    explicit UndoManager(size_t max_history = 64);

    void Clear();
    void Push(const std::string& ini_content, const std::string& action_name);
    bool CanUndo() const;
    bool CanRedo() const;
    bool Undo(std::string& out_ini);
    bool Redo(std::string& out_ini);
    void Update(float dt);

    const std::string& GetToastText() const;
    float GetToastTimer() const;

private:
    size_t m_maxHistory = 64;
    std::deque<UndoSnapshot> m_stack;
    size_t m_current = 0;
    std::string m_toastText;
    float m_toastTimer = 0.0f;
};
