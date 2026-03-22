#include "mini_game_return_ui.h"
#include "return_target.h"

static bool s_hasMiniGameReturnUi = false;
static ReturnTarget s_miniGameReturn{};

void miniGameSetReturnUi(UIState state, Tab tab)
{
  s_hasMiniGameReturnUi = true;
  s_miniGameReturn.state = state;
  s_miniGameReturn.tab = tab;
}

void miniGameClearReturnUi()
{
  s_hasMiniGameReturnUi = false;
  s_miniGameReturn = ReturnTarget{};
}

UIState miniGameGetReturnUiOrDefault(UIState fallbackState)
{
  return s_hasMiniGameReturnUi ? s_miniGameReturn.state : fallbackState;
}

Tab miniGameGetReturnTabOrDefault(Tab fallbackTab)
{
  return s_hasMiniGameReturnUi ? s_miniGameReturn.tab : fallbackTab;
}