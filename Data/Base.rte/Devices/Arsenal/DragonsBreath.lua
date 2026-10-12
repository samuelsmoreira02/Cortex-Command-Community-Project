-- Dragon's Breath pellet. Sets the terrain it hits on fire, and leaves flames that stick to and burn whoever it hits.

local function SpawnFlame(self)
	local flame = CreateMOSParticle("Flame Hurt Short Float", "Base.rte");
	flame.Pos = Vector(self.Pos.X, self.Pos.Y);
	flame.Vel = self.Vel * 0.2;
	flame.Team = self.Team;
	flame.SourceActorID = self.SourceActorID;
	MovableMan:AddParticle(flame);
end

function OnCollideWithTerrain(self, terrainID)
	if self.burned then
		return;
	end
	self.burned = true;
	SceneMan:IgniteTerrain(self.Pos, 3);
	if math.random() < 0.3 then
		SpawnFlame(self);
	end
end

function OnCollideWithMO(self, mo, rootMO)
	if self.burned then
		return;
	end
	self.burned = true;
	if math.random() < 0.45 then
		SpawnFlame(self);
	end
end
