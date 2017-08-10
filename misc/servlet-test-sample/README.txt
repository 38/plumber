This is an example of how to use the event simulation testbed verify the
servlet. 
However, the validation is manual. So we probably need to make a framework
runs all the servlet validation during the testing target is buidling.

To run this validation, just run the pss file rest_test.pss

	pscript rest_test.pss

Then a file rest.out will be created and contains the servlet output

The missing part is we are not able to verify the outputs are expected
automatically.

In addition, like the RESTful controller, it may produce a random value like
UUID, so we could have our customized validator in this case.
