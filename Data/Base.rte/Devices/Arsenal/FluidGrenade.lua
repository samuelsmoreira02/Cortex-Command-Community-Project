-- Fluid grenades. A couple of seconds after the pin is pulled the casing bursts and spills what's inside, which then flows on its own:
-- water that puts out fires, acid that eats through terrain and whoever's standing in it, burning napalm, or a cloud of toxic gas.

local fuseMS = 2000;
local payloads = {
	["Hydro Grenade"] = {fluid = "Water", radius = 18},
	["Acid Flask"] = {fluid = "Acid", radius = 10},
	["Napalm Canister"] = {fluid = "Oil", radius = 13, ignite = true},
	["Toxin Grenade"] = {fluid = "Toxic Gas", radius = 26},
};

function Update(self)
	if not self:IsActivated() or self.ToDelete then
		return;
	end
	if not self.fluidFuseTimer then
		self.fluidFuseTimer = Timer();
	end
	if self.fluidFuseTimer:IsPastSimMS(fuseMS) then
		local payload = payloads[self.PresetName];
		if payload then
			SceneMan:AddFluid(payload.fluid, self.Pos, payload.radius);
			if payload.ignite then
				SceneMan:IgniteTerrain(self.Pos, payload.radius + 3);
			end
		end
		CreateSoundContainer("Metal Body Blunt Hit Medium", "Base.rte"):Play(self.Pos);
		self.ToDelete = true;
	end
end
