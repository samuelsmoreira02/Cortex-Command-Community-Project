-- Slows time down while any human player is giving orders: with the pie menu open, setting AI waypoints or selecting units for a squad.
-- Both the time scale and the simulation step are scaled, so the game keeps updating (and responding to input) at the normal rate, just in slow motion.

function TacticalTimeScript:StartScript()
	self.slowMotionScale = 0.1;
	self.slowMotion = false;
	self.originalTimeScale = TimerMan.TimeScale;
	self.originalDeltaTimeSecs = TimerMan.DeltaTimeSecs + 0;
end

function TacticalTimeScript:UpdateScript()
	local wantsSlowMotion = false;
	local activity = ActivityMan:GetActivity();
	if activity and activity.ActivityState == Activity.RUNNING then
		for player = Activity.PLAYER_1, Activity.MAXPLAYERCOUNT - 1 do
			if activity:PlayerActive(player) and activity:PlayerHuman(player) and self:IsGivingOrders(activity, player) then
				wantsSlowMotion = true;
				break;
			end
		end
	end

	if wantsSlowMotion ~= self.slowMotion then
		self:SetSlowMotion(wantsSlowMotion);
	end
end

function TacticalTimeScript:IsGivingOrders(activity, player)
	local viewState = activity:GetViewState(player);
	if viewState == Activity.AIGOTOPOINT or viewState == Activity.UNITSELECTCIRCLE then
		return true;
	end
	local actor = activity:GetControlledActor(player);
	return actor ~= nil and MovableMan:IsActor(actor) and actor.PieMenu ~= nil and actor.PieMenu:IsVisible();
end

function TacticalTimeScript:SetSlowMotion(slowMotion)
	self.slowMotion = slowMotion;
	if slowMotion then
		TimerMan.DeltaTimeSecs = self.originalDeltaTimeSecs * self.slowMotionScale;
		TimerMan.TimeScale = self.originalTimeScale * self.slowMotionScale;
	else
		TimerMan.DeltaTimeSecs = self.originalDeltaTimeSecs;
		TimerMan.TimeScale = self.originalTimeScale;
	end
end

function TacticalTimeScript:PauseScript(paused)
	-- Don't leave the game in slow motion while it's paused, e.g. in the pause menu.
	if paused and self.slowMotion then
		self:SetSlowMotion(false);
	end
end

function TacticalTimeScript:EndScript()
	if self.slowMotion then
		self:SetSlowMotion(false);
	end
end
