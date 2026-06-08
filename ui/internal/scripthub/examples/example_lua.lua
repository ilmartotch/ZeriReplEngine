local text = "Contacts: alpha@example.com, beta@test.io, invalid@@mail, root@domain.dev"

for email in string.gmatch(text, "[%w%.%-_]+@[%w%.%-_]+%.[%a]+") do
    print(email)
end
