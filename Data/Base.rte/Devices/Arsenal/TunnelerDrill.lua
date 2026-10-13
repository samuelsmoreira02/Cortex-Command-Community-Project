-- Tunneler drill. Bores in a straight line, ignoring gravity, deleting the ground ahead of it to leave a tunnel, and blows up when it touches an enemy.
-- It's pinned and moved by hand, so the ground can't stop it. When its lifetime runs out the engine blows it up where it is.

local speed = 220;
local boreRadius = 5;
local triggerRadius = 14;

function Update(self)
	if self.ToDelete then
		return;
	end
	if not self.drillDirection then
		self.drillDirection = Vector(self.Vel.X, self.Vel.Y):SetMagnitude(1);
		self.PinStrength = 100000;
	end
	self.Vel = Vector();
	self.Pos = self.Pos + self.drillDirection * (speed * TimerMan.DeltaTimeSecs);
	self.RotAngle = self.drillDirection.AbsRadAngle;
	SceneMan:DislodgePixelCircle(self.Pos + self.drillDirection * boreRadius, boreRadius, true);

	for actor in MovableMan.Actors do
		if actor.Team ~= self.Team and SceneMan:ShortestDistance(self.Pos, actor.Pos, SceneMan.SceneWrapsX):MagnitudeIsLessThan(triggerRadius + actor.Radius * 0.5) then
			self:GibThis();
			return;
		end
	end
end
