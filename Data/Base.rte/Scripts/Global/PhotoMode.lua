-- Photo mode, toggled with F8. Hides the HUD, slows the game down to a near stop, freezes whoever you control, and lets you fly the camera around with the arrow keys or WASD (hold Shift to go faster).
-- Take your pictures with F12, which saves a screenshot to the ScreenShots folder. Press F8 again to go back to the game.

function PhotoModeScript:StartScript()
	self.active = false;
	self.cameraSpeed = 500;
	self.fastCameraMultiplier = 3;
	self.slowMotionTimeScale = 0.02;
	self.savedTimeScale = 1;
	self.savedHudDisabled = {};
	self.cameraPositions = {};
	self.frameTimer = Timer();
end

function PhotoModeScript:UpdateScript()
	local activity = ActivityMan:GetActivity();
	if not activity or activity.ActivityState ~= Activity.RUNNING then
		if self.active then
			self:Stop(activity);
		end
		return;
	end

	if UInputMan:KeyPressed(Key.F8) then
		if self.active then
			self:Stop(activity);
		else
			self:Start(activity);
		end
	end

	if self.active then
		self:Update(activity);
	end
end

function PhotoModeScript:Start(activity)
	self.active = true;
	-- Another script's brief slow motion (like a hit stop) may be on right now, and shouldn't be brought back for good when leaving photo mode.
	self.savedTimeScale = TimerMan.TimeScale >= 0.5 and TimerMan.TimeScale or 1;
	TimerMan.TimeScale = self.slowMotionTimeScale;
	self.frameTimer:Reset();
	for player = Activity.PLAYER_1, Activity.MAXPLAYERCOUNT - 1 do
		if activity:PlayerActive(player) and activity:PlayerHuman(player) then
			local screen = activity:ScreenOfPlayer(player);
			self.savedHudDisabled[screen] = FrameMan:IsHudDisabled(screen);
			FrameMan:SetHudDisabled(true, screen);
			self.cameraPositions[screen] = CameraMan:GetOffset(screen) + Vector(FrameMan.PlayerScreenWidth * 0.5, FrameMan.PlayerScreenHeight * 0.5);
		end
	end
end

function PhotoModeScript:Stop(activity)
	self.active = false;
	TimerMan.TimeScale = self.savedTimeScale;
	for screen, wasDisabled in pairs(self.savedHudDisabled) do
		FrameMan:SetHudDisabled(wasDisabled, screen);
	end
	if activity then
		for player = Activity.PLAYER_1, Activity.MAXPLAYERCOUNT - 1 do
			if activity:PlayerActive(player) and activity:PlayerHuman(player) then
				local actor = activity:GetControlledActor(player);
				if actor and MovableMan:IsActor(actor) then
					actor:GetController().Disabled = false;
				end
			end
		end
	end
	self.savedHudDisabled = {};
	self.cameraPositions = {};
end

function PhotoModeScript:Update(activity)
	-- The game is nearly frozen, so the camera has to move by real time.
	local deltaS = math.min(self.frameTimer.ElapsedRealTimeS, 0.1);
	self.frameTimer:Reset();

	local direction = Vector(0, 0);
	if UInputMan:KeyHeld(Key.LEFT) or UInputMan:KeyHeld(Key.A) then direction.X = direction.X - 1; end
	if UInputMan:KeyHeld(Key.RIGHT) or UInputMan:KeyHeld(Key.D) then direction.X = direction.X + 1; end
	if UInputMan:KeyHeld(Key.UP) or UInputMan:KeyHeld(Key.W) then direction.Y = direction.Y - 1; end
	if UInputMan:KeyHeld(Key.DOWN) or UInputMan:KeyHeld(Key.S) then direction.Y = direction.Y + 1; end
	local speed = self.cameraSpeed * ((UInputMan:KeyHeld(Key.LSHIFT) or UInputMan:KeyHeld(Key.RSHIFT)) and self.fastCameraMultiplier or 1);

	for player = Activity.PLAYER_1, Activity.MAXPLAYERCOUNT - 1 do
		if activity:PlayerActive(player) and activity:PlayerHuman(player) then
			local screen = activity:ScreenOfPlayer(player);
			-- Keep the soldier from walking away, or shooting, while the keys are used for the camera.
			local actor = activity:GetControlledActor(player);
			if actor and MovableMan:IsActor(actor) then
				actor:GetController().Disabled = true;
			end
			local position = self.cameraPositions[screen];
			if position then
				position = position + direction * (speed * deltaS);
				self.cameraPositions[screen] = position;
				CameraMan:SetScroll(position, screen);
			end
		end
	end
end

function PhotoModeScript:EndScript()
	if self.active then
		self:Stop(ActivityMan:GetActivity());
	end
end
