#include "mini_game_return_ui.h"

static bool s_hasMiniGameReturnUi = false;
static UIState s_miniGameReturnState = UIState::PET_SCREEN;
static Tab s_miniGameReturnTab = Tab::TAB_PET;

void miniGameSetReturnUi(UIState state, Tab tab)
{
  s_hasMiniGameReturnUi = true;
  s_miniGameReturnState = state;
  s_miniGameReturnTab = tab;
}

void miniGameClearReturnUi()
{
  s_hasMiniGameReturnUi = false;
  s_miniGameReturnState = UIState::PET_SCREEN;
  s_miniGameReturnTab = Tab::TAB_PET;
}

UIState miniGameGetReturnUiOrDefault(UIState fallbackState)
{
  return s_hasMiniGameReturnUi ? s_miniGameReturnState : fallbackState;
}

Tab miniGameGetReturnTabOrDefault(Tab fallbackTab)
{
  return s_hasMiniGameReturnUi ? s_miniGameReturnTab : fallbackTab;
}