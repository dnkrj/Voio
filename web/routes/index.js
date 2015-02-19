var express   = require('express');
var fs        = require('fs');
var transport = require('../config/transport'); // email configuration
var User      = require('../config/user'); // user model (db)
var util = require('util'), //Calling bash script
    exec = require('child_process').exec,
    child

module.exports = function(passport) {
	var router = express.Router();
	
	/* GET home page. */
	router.get('/', function(req, res, next) {
		res.render('index', {
			user: req.user,
	      	title: 'Voio'
	    });
	});

	/* GET user page. */
	router.get('/u/:id', function(req, res, next) {
		var username = req.params.id;
		fs.readdir(__dirname + '/../public/user/' + username + '/a', function(err, files){
			var gifs = [];
			if (files === undefined) {
				username = "no gifs here."
			} else {
				files.forEach(function(gifDir) {
    				gifs.push('"'+gifDir+'"');
				});
			}
			gifs.reverse();
			if (req.user) {	
				res.render('user', {
		      		title    : username + '&middot; Voio',
		      		userpage : username,
		  	    	gifs     : gifs,
		  	    	user     : req.user.local,
		  	    	hostname : req.hostname
		    	});
		    } else {
		    	res.render('user', {
		      		title    : username + '&middot; Voio',
		      		userpage : username,
		  	    	gifs     : gifs
		    	});
		    }
		});
	});

	router.get('/u/:id/:gif', function(req, res, next) {
		var username = req.params.id;
		var gifview = req.params.gif;
		res.render('gif', {
	      	title      : username + '&middot; Voio',
	      	userpage   : username,
	  	    gifview    : gifview,
	  	    user       : req.user
	    });
	});

	//Pages for logged out users

	/* GET signup page */
	router.get('/signup', isLoggedIn(false), function (req, res, next) {
		res.render('signup', { 
			title	: 'Signup &middot; Voio',
			message : req.flash('signupMessage')
		});
	});

	/* POST signup page */
	router.post('/signup', passport.authenticate('local-signup', {
		successRedirect : '/upload', // redirect to the secure profile section
		failureRedirect : '/signup',  // redirect back to the signup page if there is an error
	}));

	/* GET login page */
    router.get('/login', isLoggedIn(false), function (req, res) {
        res.render('login', { 
        	title	: 'Login &middot; Voio',
        	message : req.flash('loginMessage')
        }); 
    });

    /* POST login page */
    router.post('/login', passport.authenticate('local-login', {
        successRedirect : '/profile', // redirect to the user page
        failureRedirect : '/login',   // redirect back to the signup page if there is an error
    }));

	
	// Pages for logged in users

	/* Redirects profile to user page */
	router.get('/profile', isLoggedIn(true), function(req, res) {
		res.redirect('/u/'+req.user.local.username);
	});

	/* GET upload page */
	router.get('/upload', isLoggedIn(true), function(req, res) {
	    res.render('upload', {
	    	title : 'Upload &middot; Voio',
	    	user  : req.user
	    });
	});

	router.post('/upload', function(req, res) {
		console.log("/// File uploaded at: " + req.files.upFile.path + ", by: " + req.user.local.username);
		res.end();
        var bashCall ='signalAnalysis '+__dirname + '/../' + req.files.upFile.path +
                    __dirname+ '/../public/upload/'+req.user.local.username+'/p';
        var path = __dirname + '/../bin'
        console.log("Bash call is : \n" + bashCall);
        child = exec(bashCall,
                     {env :{PATH: path}},// adding environment
                    function (error, stdout, stderr) {      // one easy function to capture data/errors
                        console.log('stdout: ' + stdout);
                        console.log('stderr: ' + stderr);
                        if (error) {
                          console.log('exec error: ' + error);
                        }
                    });
        
	});

	/* GET pending page */
	router.get('/pending', isLoggedIn(true), function(req, res) {
		var username = req.user.local.username;
	    fs.readdir(__dirname + '/../public/user/' + username + '/p', function(err, files){
	    	var gifs = [];
			if (files !== undefined) {
				files.forEach(function(gifDir) {
    				gifs.push('"'+gifDir+'"');	
				});
			}
			res.render('pending', {
				title    : 'Pending &middot; Voio',
				username : username,
				user     : req.user,
				gifs     : gifs
			});
		});   
	});

	/* GET Approve Gif */
	router.get('/a/:gif', isLoggedIn(true), function(req, res) {
		var username = req.user.local.username;
		var gifname = req.params.gif;
		fs.rename(__dirname + '/../public/user/' + username + '/p/' + gifname + '.gif',
				  __dirname + '/../public/user/' + username + '/a/' + Date.now() + '.gif',
				  function (err) {
				  	if (err) {
				  		console.log("/// FAILed to approve: " + gifname + ", for user: " + username);
				  	} else {
				  		console.log("/// Approved: " + gifname + ", for user: " + username);
				  	}
				  	res.end();
				  });
	});

	/* GET Delete Gif */
	router.get('/d/:gif', isLoggedIn(true), function(req, res) {
		var username = req.user.local.username;
		var gifname = req.params.gif;
		fs.rename(__dirname + '/../public/user/' + username + '/p/' + gifname + '.gif',
				  __dirname + '/../public/user/' + username + '/d/' + Date.now() + '.gif',
				  function (err) {
				  	if (err) {
				  		console.log("/// FAILed to delete: " + gifname + ", for user: " + username);
				  	} else {
				  		console.log("/// Deleted: " + gifname + ", for user: " + username);
				  	}
				  	res.end();
				  });
	});

    /* GET logout page */
    // logs the user out and then redirects to the home page
    router.get('/logout', function(req, res) {
        req.logout();
        res.redirect('/');
    });

    // Requests for emailing
    router.get('/send', function(req, res) {
    	User.findOne({ 'local.email' : req.query.to }, function(err, user) {
    		if (err) {
    			console.log(err);
    			res.redirect('/profile');
    		}
    		if (typeof user !== 'undefined') {
		        link="http://" + req.hostname + "/verify?email=" + req.query.to + 
		        										"&code=" + req.query.code +
		        										"&_id="  + user._id;
		        console.log(req.query.code);
		        console.log(user.code);
		        console.log(link);
		        mailOptions = {
		            to      : req.query.to,
		            from    : "no-reply@voio.io",
		            subject : "Please confirm your Email Account",
		            html    : "Hello " + user.local.username + ",<br> Please Click on the link to verify your email.<br><a href="+link+">Click here to verify</a>" 
		        }
		        transport.sendMail(mailOptions, function(error, response) {
		            if (error) {
		                console.log(error);
		                req.flash('profileMessage', "Unable to send email");
		                res.redirect('/profile');
		            } else {
		                res.redirect('/profile');
		            }
		        });    			
    		}
    	});
    });

    router.get('/verify', function(req, res) {
    	var id    = req.query._id;
        var email = req.query.email;
        var code  = req.query.code;
        console.log("verifying");
        User.findOneAndUpdate( 
        	{ 
        		'_id'            : id,
        		'local.email'    : email, 
        		'local.vericode' : code }, 
        	{ 'local.verified' : true },
        	{},
        	function(err) {
        		if (err) console.log(err);
        		res.redirect("/login");
        	}
        );
    });
    
	return router;
};

function isLoggedIn (loggedin) {
    return function (req, res, next) {
    	// if user is authentication matches in the session, carry on 
    	if (req.isAuthenticated() === loggedin)
    	    return next();

    	// if they aren't redirect them to the home page
    	res.redirect('/');
    }
}