-- Tunneler. Each shot launches a drilling missile (see TunnelerDrill.lua) from the muzzle, pointed wherever the gun is aiming.

function OnFire(self)
	local direction = Vector(self.FlipFactor, 0):RadRotate(self.RotAngle);
	local drill = CreateAEmitter("Tunneler Drill", "Base.rte");
	drill.Pos = self.MuzzlePos + direction * 4;
	drill.Vel = direction;
	drill.RotAngle = direction.AbsRadAngle;
	drill.Team = self.Team;
	drill.IgnoresTeamHits = true;
	local shooter = self:GetRootParent();
	drill.SourceActorID = (shooter and IsActor(shooter)) and shooter.UniqueID or self.SourceActorID;
	MovableMan:AddParticle(drill);
end
