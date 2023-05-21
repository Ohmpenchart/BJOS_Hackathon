import seaborn as sns; sns.set()  # for plot styling
import requests
import json
import numpy as np
import time
from sklearn.preprocessing import PolynomialFeatures
from sklearn.linear_model import LinearRegression
from sklearn.pipeline import make_pipeline
from sklearn.model_selection import GridSearchCV
def PolynomialRegression(degree=2, **kwargs):
    return make_pipeline(PolynomialFeatures(degree),
                         LinearRegression(**kwargs))

def make_data(N, err=1.0, rseed=1):
    # randomly sample the data
    rng = np.random.RandomState(rseed)
    X = rng.rand(N, 1)*100
    y = 8200 - 4100*(X.ravel()**4)/(100**4) - 4100*(X.ravel()**2)/(100**2)
    if err > 0:
        y += err * rng.randn(N)*8200
    return X, y

def getshadow():
    # Set the API endpoint URL
    url = "https://api.netpie.io/v2/device/shadow/data"

    # Set the authorization header
    headers = {
        "Authorization": "Basic ZDdlNzVjYjgtMDI2YS00ZjZhLWFmOWQtYWU1MDlkZGJjNjczOlN4cFc3WEhRMm80UVJvZlp0N0dITXFjeEZtTU12ZFF6"
    }

    # Make a GET request to the API endpoint with headers
    response = requests.get(url, headers=headers)

    # Check if the request was successful (status code 200)
    if response.status_code == 200:
        # Extract the response data in JSON format
        data = response.json()
        # Process the data as needed
        print(data["data"])
        return data["data"]
    else:
        # Handle the error case
        print("Error:", response.status_code)
        return {}

def shadow_status_update(statusMode,bestIR):
    # Set the API endpoint URL
    url = "https://api.netpie.io/v2/device/shadow/data"
    # Set the authorization header and content type header
    headers = {
        "Authorization": "Basic ZDdlNzVjYjgtMDI2YS00ZjZhLWFmOWQtYWU1MDlkZGJjNjczOlN4cFc3WEhRMm80UVJvZlp0N0dITXFjeEZtTU12ZFF6",
        "Content-Type": "application/json"
    }
    # Set the data payload for the PUT request
    data = {
        "data": {
            "statusMode": statusMode,
            "update": 0,
            "bestIR":bestIR
        }
    }
    # Convert the data payload to JSON
    json_data = json.dumps(data)

    # Make a PUT request to the API endpoint with headers and data payload
    response = requests.put(url, headers=headers, data=json_data)

    # Check if the request was successful (status code 200)
    if response.status_code == 200:
        # Process the response as needed
        print("Request successful")
    else:
        # Handle the error case
        print("Error:", response.status_code)

def main():
    #setup
    #create dataset
    X, y = make_data(200,0.1)
    param_grid = {'polynomialfeatures__degree': np.arange(21),
                  'linearregression__fit_intercept': [True, False]}
    grid = GridSearchCV(PolynomialRegression(), param_grid, cv=7)
    #train model best param by gridsearch
    grid.fit(X, y)
    print(grid.best_params_)
    model = grid.best_estimator_
    #forever loop
    while(True):
        print("getting data")
        Data = getshadow()
        update = Data["update"]
        if(Data["update"]==1):
            print("updated data")
            T_ref=(Data["environment"]["temperature_ref"])
            print("Tref=",T_ref)
            IR=Data["environment"]["IR_output"]
            print("IR=",IR)
            max_IR = model.predict(np.array([[T_ref]]))[0]
            bestIR = 0.8*max_IR
            print("bestIR=", bestIR)
            #with safty factor=0.8
            if(abs(IR-bestIR)<1000):
                shadow_status_update(0, bestIR)
                print("OK")
            elif(IR-bestIR>1000):
                shadow_status_update(1, bestIR)
                print("Careful IR is too high")
            elif(bestIR-IR>1000):
                shadow_status_update(2,bestIR)
                print("Increase IR for more efficient")
        else:
            print("shadow is not updated")

        time.sleep(5)


main()



