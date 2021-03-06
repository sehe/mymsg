void Oracle_OCI_ATTR_CHARSET_FORM_OCI_ATTR_CHARSET_ID()
{
	SAConnection con;
	SACommand cmd;
	cmd.setConnection(&con);

    try
    {
		con.Connect("cit", "dwadm", "dwadm", SA_Oracle_Client);

		bool bDropTable = !false;
		bool bCreateTable = !false;
		if(bDropTable)
		{
			cmd.setCommandText(
				"drop table test_charset");
			cmd.Execute();
		}
		if(bCreateTable)
		{
			cmd.setCommandText(
				"Create table test_charset (f1 nchar(254), f2 char(254))");
			cmd.Execute();
		}

		cmd.setCommandText("delete from test_charset");
		cmd.Execute();

		cmd.setCommandText("insert into test_charset values (:1, :2)");
		cmd.Param(1).setOption("OCI_ATTR_CHARSET_FORM") = "SQLCS_NCHAR";
		cmd.Param(1).setOption("OCI_ATTR_CHARSET_ID") = "171";
		cmd.Param(2).setOption("OCI_ATTR_CHARSET_ID") = "CL8MSWIN1251";

		cmd.Param(1).setAsString() = "string f1 - 1";
		cmd.Param(2).setAsValueRead() = cmd.Param(1);
		cmd.Execute();

		cmd.Param(1).setAsString() = "������ f1 - 2";
		cmd.Param(2).setAsValueRead() = cmd.Param(1);
		cmd.Execute();

		cmd.setCommandText("select * from test_charset");
		cmd.Execute();

		cmd.Field(1).setOption("OCI_ATTR_CHARSET_ID") = "171";
		cmd.Field(2).setOption("OCI_ATTR_CHARSET_ID") = "CL8MSWIN1251";
		while(cmd.FetchNext())
		{
			cout 
				<< (const char*)cmd[1].asString()
				<< ""
				<< (const char*)cmd[2].asString()
				<< endl;
		}
    }
    catch(SAException &x)
    {
        try
        {
            con.Rollback();
        }
        catch(SAException &)
        {
        }
        printf("%s\n", (const char*)x.ErrText());
    }
}