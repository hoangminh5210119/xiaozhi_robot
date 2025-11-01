/*

   Vehicle:
     {"t":"v","d":1,"p":15,"ms":2000}  ← Forward millis second
     {"t":"v","d":1,"p":10,"mm":2000}  ← Forward mm
     {"t":"v","d":2,"p":60,"ms":2000}  ← Backward  millis second
     {"t":"v","d":2,"p":40,"mm":2000}  ← Backward mm

     {"t":"v","d":3,"p":60,"ms":2000}  ← Left 
     {"t":"v","d":3,"p":60,"mm":2000}  ← Left mm
     {"t":"v","d":4,"p":60,"ms":2000}  ← Right 
     {"t":"v","d":4,"p":60,"mm":200}  ← Right mm


     {"t":"v","d":5,"p":60,"ms":2000}  ← ROTATE_LEFT  
     {"t":"v","d":6,"p":60,"ms":2000}  ← ROTATE_RIGHT

     {"t":"v","d":0}                    ← Stop
   Storage:
     {"t":"s","i":0,"a":1}              ← Open slot 0
     {"t":"s","i":0,"a":0}              ← Close slot 0
      {"t":"s","i":1,"a":1}              ← Open slot 1
      {"t":"s","i":1,"a":0}              ← Close slot 1
      {"t":"s","i":2,"a":1}              ← Open slot 2
      {"t":"s","i":2,"a":0}              ← Close slot 2
      {"t":"s","i":3,"a":1}              ← Open slot 3
      {"t":"s","i":3,"a":0}              ← Close slot 3
   Status:
     {"t":"t"}                           ← Get status
*/ 

// {"t":"t"}

{"t":"s","i":0,"a":0}
{"t":"s","i":0,"a":1}

{"t":"s","i":1,"a":0}
{"t":"s","i":1,"a":1}

{"t":"s","i":2,"a":0}
{"t":"s","i":2,"a":1}

{"t":"s","i":3,"a":0}
{"t":"s","i":3,"a":1}