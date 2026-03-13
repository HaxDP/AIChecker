#include "server/Api.h"

int main()
{
   backend::WebApiApp app;
   app.Run(8080);
   return 0;
}