-- Marks every unit with a symbol above its head that tells its team apart by shape as well as color, so teams can be told apart without relying on telling colors apart.
-- Each team gets its own shape, in a palette that stays distinguishable for red-green and blue-yellow colorblindness: team 1 a blue circle, team 2 an orange square, team 3 a yellow triangle, team 4 a light blue diamond.

function ColorblindTeamMarkersScript:StartScript()
	self.markerColors = {[Activity.TEAM_1] = 216, [Activity.TEAM_2] = 86, [Activity.TEAM_3] = 117, [Activity.TEAM_4] = 194};
	self.outlineColor = 245;
	self.markerOffset = Vector(0, -34);
	self.markerSize = 4;
end

-- Draws a team's shape, with a dark outline drawn first so it stays readable against any sky or terrain.
function ColorblindTeamMarkersScript:DrawMarker(screen, team, pos)
	local color = self.markerColors[team];
	if not color then
		return;
	end
	local size = self.markerSize;
	if team == Activity.TEAM_1 then
		PrimitiveMan:DrawCircleFillPrimitive(screen, pos, size + 1, self.outlineColor);
		PrimitiveMan:DrawCircleFillPrimitive(screen, pos, size, color);
	elseif team == Activity.TEAM_2 then
		PrimitiveMan:DrawBoxFillPrimitive(screen, pos + Vector(-size - 1, -size - 1), pos + Vector(size + 1, size + 1), self.outlineColor);
		PrimitiveMan:DrawBoxFillPrimitive(screen, pos + Vector(-size, -size), pos + Vector(size, size), color);
	elseif team == Activity.TEAM_3 then
		PrimitiveMan:DrawTrianglePrimitive(screen, pos + Vector(0, -size - 2), pos + Vector(-size - 2, size + 1), pos + Vector(size + 2, size + 1), self.outlineColor);
		PrimitiveMan:DrawTrianglePrimitive(screen, pos + Vector(0, -size), pos + Vector(-size, size), pos + Vector(size, size), color);
	else
		PrimitiveMan:DrawTrianglePrimitive(screen, pos + Vector(0, -size - 2), pos + Vector(-size - 2, 0), pos + Vector(size + 2, 0), self.outlineColor);
		PrimitiveMan:DrawTrianglePrimitive(screen, pos + Vector(0, size + 2), pos + Vector(-size - 2, 0), pos + Vector(size + 2, 0), self.outlineColor);
		PrimitiveMan:DrawTrianglePrimitive(screen, pos + Vector(0, -size), pos + Vector(-size, 0), pos + Vector(size, 0), color);
		PrimitiveMan:DrawTrianglePrimitive(screen, pos + Vector(0, size), pos + Vector(-size, 0), pos + Vector(size, 0), color);
	end
end

function ColorblindTeamMarkersScript:UpdateScript()
	local activity = ActivityMan:GetActivity();
	if not activity or activity.ActivityState ~= Activity.RUNNING then
		return;
	end

	for player = Activity.PLAYER_1, Activity.MAXPLAYERCOUNT - 1 do
		if activity:PlayerActive(player) and activity:PlayerHuman(player) and activity:GetViewState(player) == Activity.NORMAL then
			local screen = activity:ScreenOfPlayer(player);
			local screenCenter = CameraMan:GetOffset(screen) + Vector(FrameMan.PlayerScreenWidth * 0.5, FrameMan.PlayerScreenHeight * 0.5);
			-- Only the units that can be on this player's screen get a marker, with some slack so they don't pop in at the edges.
			local maxDistance = math.max(FrameMan.PlayerScreenWidth, FrameMan.PlayerScreenHeight) * 0.75;
			for actor in MovableMan.Actors do
				if actor.Status ~= Actor.DEAD and actor.Status ~= Actor.DYING and (actor.ClassName == "AHuman" or actor.ClassName == "ACrab") then
					if SceneMan:ShortestDistance(screenCenter, actor.Pos, SceneMan.SceneWrapsX).Magnitude < maxDistance then
						self:DrawMarker(screen, actor.Team, actor.AboveHUDPos + self.markerOffset);
					end
				end
			end
		end
	end
end
