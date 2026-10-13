-- Chakram Launcher. Each shot throws a spinning energy blade (see Chakram.lua) from the muzzle.

function OnFire(self)
	local direction = Vector(self.FlipFactor, 0):RadRotate(self.RotAngle);
	local blade = CreateMOPixel("Chakram Blade", "Base.rte");
	blade.Pos = self.MuzzlePos + direction * 4;
	blade.Vel = direction;
	blade.Team = self.Team;
	local shooter = self:GetRootParent();
	blade.SourceActorID = (shooter and IsActor(shooter)) and shooter.UniqueID or self.SourceActorID;
	MovableMan:AddParticle(blade);
end
