# The first of these works in BSD make and assigns $(OS_A!) in GNU make.
# The second works in GNU make and appears harmless in BSD make.
# Neither will work in legacy SVR4 make, but we don't care about that.
OS_A!=uname | tr A-Z a-z
OS_B=$(shell uname | tr A-Z a-z)
OS=$(OS_A)$(OS_B)

