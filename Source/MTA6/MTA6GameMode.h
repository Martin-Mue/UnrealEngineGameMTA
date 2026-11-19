#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "MTA6GameMode.generated.h"

/** Standard-GameMode von MTA 6. Wird in Config/DefaultEngine.ini als globaler Default gesetzt. */
UCLASS()
class MTA6_API AMTA6GameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AMTA6GameMode();
};
