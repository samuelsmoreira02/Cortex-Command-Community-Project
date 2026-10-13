-- Sentry Deployer. Once thrown and settled, it unfolds into a turret on the thrower's team (see SentryLifetime.lua, which breaks it down after a while).

local settleDelayMS = 900;

function Update(self)
	if not self:IsActivated() or self.ToDelete then
		return;
	end
	if not self.deployTimer then
		self.deployTimer = Timer();
	end
	if self.deployTimer:IsPastSimMS(settleDelayMS) and self.Vel:MagnitudeIsLessThan(2) then
		local turret = CreateACrab("TradeStar Turret", "Base.rte");
		if turret then
			turret.Pos = self.Pos + Vector(0, -8);
			turret.Team = self.Team;
			turret.AIMode = Actor.AIMODE_SENTRY;
			turret:AddScript("Base.rte/Devices/Arsenal/SentryLifetime.lua");
			MovableMan:AddActor(turret);
		end
		self.ToDelete = true;
	end
end
