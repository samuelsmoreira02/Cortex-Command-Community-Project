-- Added to turrets put down by the Sentry Deployer. They break down after a while, so they can't hold a position forever.

local lifetimeMS = 30000;

function Update(self)
	if not self.sentryTimer then
		self.sentryTimer = Timer();
	end
	if self.sentryTimer:IsPastSimMS(lifetimeMS) then
		self:GibThis();
	end
end
