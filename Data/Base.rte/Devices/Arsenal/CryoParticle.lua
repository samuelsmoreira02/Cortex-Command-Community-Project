-- Cryo Projector mist. Each particle that touches an enemy chills it a little, and enough chill freezes it solid (see Frozen.lua, which this adds to the unit).

function OnCollideWithMO(self, mo, rootMO)
	if self.chilled then
		return;
	end
	self.chilled = true;
	if rootMO and IsActor(rootMO) then
		local actor = ToActor(rootMO);
		if actor.Team ~= self.Team and actor.Status ~= Actor.DEAD then
			-- A frozen unit is already as cold as it gets.
			if not actor:NumberValueExists("ArsenalFrozen") then
				local chill = actor:NumberValueExists("ArsenalChill") and actor:GetNumberValue("ArsenalChill") or 0;
				actor:SetNumberValue("ArsenalChill", chill + 1);
			end
			actor.Health = actor.Health - 0.4;
			if self.SourceActorID ~= 0 then
				actor.LastAttackerID = self.SourceActorID;
			end
			if not actor:NumberValueExists("ArsenalFreezable") then
				actor:SetNumberValue("ArsenalFreezable", 1);
				actor:AddScript("Base.rte/Devices/Arsenal/Frozen.lua");
			end
		end
	end
	self.ToDelete = true;
end
