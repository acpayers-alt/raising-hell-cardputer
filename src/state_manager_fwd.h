#pragma once
// Forward declaration only — do NOT include state_manager.h from state headers.
// Keep state headers lightweight and avoid circular include chains.
                                                                  → state_manager.h  ← LOOP
class StateManager;
extern StateManager state_manager;